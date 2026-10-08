#include "JoltPhysicsManager.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyManager.h>

#include <algorithm> // std::min / std::max
#include <cmath>     // std::ceil
#include <cstdarg>
#include <cstdio> // snprintf
#include <thread>
#include <windows.h> // OutputDebugStringA

#include "tracy/Tracy.hpp"                 // ZoneScopedN / tracy::SetThreadName
#include "Engine/ECS/Jobs/CCL_JobSystem.h" // 共有プールのワーカー数取得
#include "Engine/Platform/Core/Logger.h"   // 起動時の構成ログ

using namespace JPH;

// 物理世界の規模の上限を切り替えるスイッチ。
//   1 なら大量の剛体を想定した上限とスクラッチメモリ、0 ならサンプル規模。
//
// 注意: Debug が 0 側なので剛体上限が 1024 になる。ドローンを増やすと Debug でだけ
//   上限に触れることがある。踏んだ場合は JoltBodySetupSystem が ERROR ログを出す。
#ifdef _DEBUG
#define PHYSICS_LARGE_WORLD_BUDGET 0
#else
#define PHYSICS_LARGE_WORLD_BUDGET 1
#endif

// --- Jolt のトレース／アサートの出力先 ---
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

    // Visual Studio の出力ウィンドウへ出す
    OutputDebugStringA(buffer);

    return true; // true でデバッガをブレークさせる
}
#endif

JoltPhysicsManager::~JoltPhysicsManager()
{
    if (!m_isInitialized) return;

    // 作成の逆順で解放する。
    // PhysicsSystem を先に解放しないと、中で使っている Allocator や Filter が先に消え、
    // PhysicsSystem のデストラクタでアクセス違反になる。
    m_physicsSystem.reset();

    // ジョブシステムは「使っていた方」だけ実体を持つ。
    // どちらも reset() して構わない（未使用側は最初から空）。
    // アダプタが指している共有プールは EngineServices の所有物なので、ここでは破棄しない。
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

    // 3. 一時アロケータとジョブシステムの構築
    // TempAllocator は、衝突点のリストなどステップ中だけ使うデータ用のスタック型アロケータ。
    // 毎回 new せず、ステップの終わりにまとめて巻き戻して再利用する。

#if PHYSICS_LARGE_WORLD_BUDGET
    // 50MB の計算用スクラッチメモリ
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(50 * 1024 * 1024);
#else
    m_tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);
#endif

    // ジョブシステムは、物理計算を複数の CPU コアで並列に走らせるためのもの。
    // hardware_concurrency() は取得できない環境で 0 を返す規格。
    //   そのまま -1 するとアンダーフローして巨大値になりスレッド生成でクラッシュするため、
    //   必ず 1 以上にする。
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
        // --- 共有プールモード: ECS と同じスレッドプールで物理ジョブを走らせる ---
        //
        // 利点
        //   ・スレッドプールが1本になる（プール2本ぶんのスレッドを立てない）
        //   ・JoltJobSystemAdapter がジョブ関数を包むので、Tracy に
        //     ジョブ名付きのゾーンとして並ぶ
        //
        // 単独モードで動かしたいときは、Framework.cpp の Initialize 呼び出しの
        // 引数を nullptr にする（下の else 側に落ちる）。
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
        // --- 単独モード: Jolt 純正のスレッドプールを使う ---
        //
        // 構築順に注意:
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
        //       ジョブ単位で見たい場合は、上の共有プールモード（アダプタ）を使う。
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
    const uint cMaxBodies             = 65536; // 剛体の最大数
    const uint cNumBodyMutexes        = 0;     // デフォルトのミューテックス数
    const uint cMaxBodyPairs          = 65536; // 密集で接触ペアが急増しても溢れないように
    const uint cMaxContactConstraints = 20480; // 大量に押し合う場面のための接触拘束の上限
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

    // 5. 重力（m/秒^2）
    m_physicsSystem->SetGravity(Vec3(0, -9.81f, 0));

    // 6. 接触リスナーを登録する
    m_contactListener = std::make_unique<JoltContactListener>();
    m_physicsSystem->SetContactListener(m_contactListener.get());

    m_isInitialized = true;
}

void JoltPhysicsManager::Step(float deltaTime)
{
    // Simulation セクション（Framework.cpp の TRACY_SECTION）の中に置かれる Zone。
    // 「メインスレッドから見た物理の総時間」がこれで測れる。
    // 物理ジョブの中身は、共有プールモードなら各ワーカーのジョブ名付きゾーンで見る。
    ZoneScopedN("Physics: Jolt Update");

    if (!m_isInitialized) return;

    // dt の上限。
    //   ブレークポイント復帰・アプリ一時停止・ロードのスパイク後などに巨大な dt が入ると、
    //   Jolt は貫通や吹き飛びを起こす。上限で切り詰める。
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
