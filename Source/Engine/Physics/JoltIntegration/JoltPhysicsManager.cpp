#include "JoltPhysicsManager.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyManager.h>

#include <algorithm> // std::min / std::max
#include <cmath>     // std::ceil
#include <cstdarg>
#include <cstdio>    // snprintf
#include <thread>
#include <windows.h> // OutputDebugStringA

#include "tracy/Tracy.hpp"             // ZoneScopedN / tracy::SetThreadName
#include "Engine/ECS/Jobs/CCL_JobSystem.h" // 共有プールのワーカー数取得
#include "Engine/Platform/Core/Logger.h"   // 起動時の構成ログ

using namespace JPH;

// 物理世界の「予算」を切り替えるスイッチ。
//   名前が示す通りの意味しか持たない: 1 なら大量の剛体を想定した上限とスクラッチメモリ、
//   0 ならサンプル規模。パフォーマンスカウンタの類とは無関係（旧名
//   PhysicsPerformanceCounters は実態と違うので改名した）。
//
// 注意: Debug が 0 側なので剛体上限が 1024 になる。ドローンを増やすと Debug でだけ
//   上限に触れることがある。踏んだ場合は JoltBodySetupSystem が ERROR ログを出す。
#ifdef _DEBUG
#define PHYSICS_LARGE_WORLD_BUDGET 0
#else
#define PHYSICS_LARGE_WORLD_BUDGET 1
#endif

// --- デバッグ用コールバック ---
static void TraceImpl(const char *inFMT, ...)
{
    va_list list;
    va_start(list, inFMT);
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), inFMT, list);
    va_end(list);

    OutputDebugStringA("[Jolt] ");
    OutputDebugStringA(buffer);
    OutputDebugStringA("\n");
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(
    const char *inExpression, const char *inMessage, const char *inFile, uint inLine)
{
    char buffer[2048];
    snprintf(buffer,
        sizeof(buffer),
        "[Jolt Assert] %s:%d: (%s) %s\n",
        inFile,
        inLine,
        inExpression,
        (inMessage ? inMessage : ""));

    // 確実にVisual Studioの出力ウィンドウにエラーを叩き出す
    OutputDebugStringA(buffer);

    return true; // trueでデバッガをブレークさせる
}
#endif

// ※ レイヤーフィルターの実装（BPLayerInterfaceImpl 等）は PhysicsLayers.cpp に移動済み。

// --- JoltPhysicsManager 実装 ---

JoltPhysicsManager::~JoltPhysicsManager()
{
    if (!m_isInitialized) return;

    // 解放順序は「作成の逆」でなければ絶対にクラッシュします
    // Systemを先に開放しないと、Systemの中で使っているAllocatorやFilterが先に消えてしまい、
    // Systemのデストラクタでアクセス違反になる
    m_physicsSystem.reset();

    // ジョブシステムは「使っていた方」だけ実体を持つ。
    // どちらも reset() して構わない（未使用側は最初から空）。
    // ※ アダプタが指している共有プールは EngineServices の所有物なので、
    //    ここでは破棄しない（アダプタは参照しているだけ）。
    m_activeJobSystem = nullptr;
    m_adapterJobSystem.reset();
    m_nativeJobSystem.reset();

    m_tempAllocator.reset();

    UnregisterTypes();

    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

void JoltPhysicsManager::Initialize(CCL::ECS::JobSystem *inSharedPool)
{
    if (m_isInitialized) return;

    // 1. 基盤コールバックの設定
    RegisterDefaultAllocator();
    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)

    // 2. ファクトリと型の登録
    Factory::sInstance = new Factory();
    RegisterTypes();

    // 3. 一時アロケータ と ジョブシステムの構築
    // TempAllocatorは毎フレーム出る衝突点のリストなどの一時的なデータを
    // 確保するためで毎回newせずに、フレームの最後に一括でリセットして再利用するスタイル

    #if PHYSICS_LARGE_WORLD_BUDGET
    // 50 * 1024 * 1024 = 約50MBの計算用スクラッチメモリ
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(50 * 1024 * 1024);
    #else
    m_tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);
    #endif

    // ジョブシステムは、物理計算を複数のCPUコアで並列に走らせるためのもの。
    // fix: hardware_concurrency() は取得不能な環境で 0 を返す規格。
    //   そのまま "-1" するとアンダーフローして巨大値になりスレッド生成でクラッシュするため、
    //   必ず 1 以上にクランプする。
    //
    // なぜ「コア数 - 1」なのか:
    //   Jolt のドキュメント（JobSystemThreadPool.h の Init）に
    //     "the number of concurrent jobs is 1 more because the main thread will
    //      also run jobs while waiting for a barrier to complete"
    //   とある通り、バリア待ちの間はメインスレッドもジョブを実行する。
    //   つまり総並列度は (ワーカー数 + 1)。コア数フルに立てるとコア数+1になり、
    //   OSのコンテキストスイッチで損をする。
    //   ECS 側の自作 JobSystem も同じ理由で -1 に揃えてある（CCL_JobSystem.cpp）。
    const unsigned int hwThreads     = std::thread::hardware_concurrency();
    const int          workerThreads = (hwThreads > 1) ? static_cast<int>(hwThreads - 1) : 1;

    if (inSharedPool != nullptr)
    {
        // ============================================================
        // 統合モード: ECS と同じスレッドプールで物理ジョブを走らせる
        // ============================================================
        //
        // 利点
        //   ・スレッドプールが1本になる（プール2本ぶんのスレッドを立てない）
        //   ・物理ジョブの実行が JoltJobSystemAdapter::RunJob を通るので、
        //     Tracy に「ジョブ名付きのゾーン」として並ぶ
        //
        // 元に戻したいときは Framework.cpp の Initialize 呼び出しから
        // 引数を外して nullptr にするだけでよい（下の else 側に落ちる）。
        m_adapterJobSystem = std::make_unique<JoltJobSystemAdapter>();
        m_adapterJobSystem->Init(inSharedPool, cMaxPhysicsJobs, cMaxPhysicsBarriers);
        m_activeJobSystem = m_adapterJobSystem.get();

        // どちらのモードで動いているか、Tracy計装が実際にビルドに入っているかを
        // 起動ログに残す。「Tracyに出ない」ときの切り分けを推測でやらないため。
    #if defined(TRACY_ENABLE) && defined(JPH_PROFILE_ENABLED)
        const char *tracyState = "ON";
    #else
        const char *tracyState = "OFF (TRACY_ENABLE / JPH_PROFILE_ENABLED のどちらかが未定義)";
    #endif
        CCL_LOG_INFO(LogCategory::Physics,
            "JobSystem = SHARED (JoltJobSystemAdapter) / workers=%u / job zones=%s",
            inSharedPool->GetWorkerCount(), tracyState);
    }
    else
    {
        // ============================================================
        // 従来モード: Jolt 純正のスレッドプールを使う
        // ============================================================
        //
        // --- 構築順に注意 ---
        //
        // 引数付きコンストラクタ JobSystemThreadPool(maxJobs, maxBarriers, numThreads) は
        // その場で Init() を呼び、ワーカースレッドを起動してしまう。
        // SetThreadInitFunction は JobSystemThreadPool.h に
        //   "must be set before calling Init()"
        // と明記されている通り、起動後に設定してももう呼ばれない。
        // そのため「デフォルト構築 → フック設定 → Init()」の順にする。
        m_nativeJobSystem = std::make_unique<JobSystemThreadPool>();

        // --- Tracy へワーカースレッドを名前付きで登録する ---
        //
        // SetThreadInitFunction は Jolt が公式に用意しているフックで、
        // 各ワーカースレッドの起動直後に、そのスレッド自身の上で呼ばれる。
        // ここで tracy::SetThreadName を呼ぶと "Jolt Worker 0..N" として
        // OSレベルのスレッド名が付く（VSのスレッドウィンドウにも出る）。
        //
        // 注意: 名前を付けても Tracy のタイムラインに行は生えない。
        //       Tracy が行を作るのは「イベントを送信したスレッド」だけで、
        //       Jolt のジョブ関数には Tracy のゾーンが1つも無いため。
        //       ジョブ単位で見たい場合が、上の統合モード（アダプタ）の用途。
        m_nativeJobSystem->SetThreadInitFunction([](int threadIndex) {
            char name[32];
            snprintf(name, sizeof(name), "Jolt Worker %d", threadIndex);
            tracy::SetThreadName(name);
        });

        // フックを仕込んでからワーカースレッドを起動する
        m_nativeJobSystem->Init(cMaxPhysicsJobs, cMaxPhysicsBarriers, workerThreads);
        m_activeJobSystem = m_nativeJobSystem.get();

        CCL_LOG_INFO(LogCategory::Physics,
            "JobSystem = NATIVE (JPH::JobSystemThreadPool) / workers=%d / job zones=なし",
            workerThreads);
    }

    // 4. 物理システムのパラメータ設定
    #if PHYSICS_LARGE_WORLD_BUDGET
    // 大量の剛体を想定した上限値
    const uint cMaxBodies             = 65536; // 最大6万体まで許容
    const uint cNumBodyMutexes        = 0;     // デフォルトのミューテックス数
    const uint cMaxBodyPairs          = 65536; // 密集によるペア爆発に備える
    const uint cMaxContactConstraints = 20480; // 大量に押し合うための接点バッファ
    #else
    // サンプル用。本番は上のスケールを使う。
    const uint cMaxBodies             = 1024;
    const uint cNumBodyMutexes        = 0;
    const uint cMaxBodyPairs          = 1024;
    const uint cMaxContactConstraints = 1024;
    #endif

    m_physicsSystem = std::make_unique<PhysicsSystem>();
    m_physicsSystem->Init(cMaxBodies,
        cNumBodyMutexes,
        cMaxBodyPairs,
        cMaxContactConstraints,
        m_bpLayerInterface,
        m_objectVsBroadphaseFilter,
        m_objectVsObjectFilter);

    // 5. 重力設定（下方向）
    m_physicsSystem->SetGravity(Vec3(0, -9.81f, 0));

    // 6. 接触リスナー（ポスト）を設置する
    m_contactListener = std::make_unique<JoltContactListener>();
    m_physicsSystem->SetContactListener(m_contactListener.get());

    m_isInitialized = true;
}

// 毎フレーム呼び出す更新関数
void JoltPhysicsManager::Step(float deltaTime)
{
    // Simulation セクション（Framework.cpp の TRACY_SECTION）の中に置かれる Zone。
    // 「メインスレッドから見た物理の総時間」がこれで測れる。
    // 物理ワーカーが並列で何をしているかは "Jolt Worker N" スレッドの帯を見る。
    ZoneScopedN("Physics: Jolt Update");

    if (!m_isInitialized) return;

    // fix: dtクランプ。
    //   ブレークポイント復帰・アプリ一時停止・ロードスパイク後などに巨大な dt が入ると、
    //   可変ステップの Jolt は簡単に爆発（貫通・吹き飛び）する。上限でクランプする。
    constexpr float kMaxStepTime = 1.0f / 30.0f; // 1フレームで進める物理時間の上限（秒）
    if (deltaTime <= 0.0f) return;
    if (deltaTime > kMaxStepTime) deltaTime = kMaxStepTime;

    // 1ステップの秒数が大きいほど衝突ステップ数を増やし、貫通を抑える。
    constexpr float kFixedStep     = 1.0f / 60.0f;
    int             collisionSteps = static_cast<int>(std::ceil(deltaTime / kFixedStep));
    if (collisionSteps < 1) collisionSteps = 1;

    // 衝突ステップ数を Zone に添付しておく。
    // 処理落ち時は dt が伸びて collisionSteps が増える＝物理が重くなる、という
    // 「スパイクの原因」が Tracy 上でそのまま読める。
    ZoneValue(static_cast<uint64_t>(collisionSteps));

    m_physicsSystem->Update(deltaTime, collisionSteps, m_tempAllocator.get(), m_activeJobSystem);
}