#include "JoltPhysicsManager.h"
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/BodyManager.h>

#include <algorithm> // std::min / std::max
#include <cmath>     // std::ceil
#include <cstdarg>
#include <thread>
#include <windows.h> // OutputDebugStringA

using namespace JPH;

#ifdef _DEBUG
#define PhysicsPerformanceCounters 0 // パフォーマンスを最低限対応にする
#else
#define PhysicsPerformanceCounters 1 // パフォーマンスを最大限対応にする
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
    m_jobSystem.reset();
    m_tempAllocator.reset();

    UnregisterTypes();

    delete Factory::sInstance;
    Factory::sInstance = nullptr;
}

void JoltPhysicsManager::Initialize()
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

    #if PhysicsPerformanceCounters
    // 50 * 1024 * 1024 = 約50MBの計算用スクラッチメモリ
    m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(50 * 1024 * 1024);
    #else
    m_tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024);
    #endif

    // ジョブシステムは、物理計算を複数のCPUコアで並列に走らせるためのもの。
    // fix: hardware_concurrency() は取得不能な環境で 0 を返す規格。
    //   そのまま "-1" するとアンダーフローして巨大値になりスレッド生成でクラッシュするため、
    //   必ず 1 以上にクランプする。
    const unsigned int hwThreads     = std::thread::hardware_concurrency();
    const int          workerThreads = (hwThreads > 1) ? static_cast<int>(hwThreads - 1) : 1;
    m_jobSystem = std::make_unique<JobSystemThreadPool>(
        cMaxPhysicsJobs, cMaxPhysicsBarriers, workerThreads);

    // 4. 物理システムのパラメータ設定
    #if PhysicsPerformanceCounters
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

    m_physicsSystem->Update(deltaTime, collisionSteps, m_tempAllocator.get(), m_jobSystem.get());
}