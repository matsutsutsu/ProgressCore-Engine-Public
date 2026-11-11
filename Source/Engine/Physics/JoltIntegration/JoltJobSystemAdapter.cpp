#include "JoltJobSystemAdapter.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h"

#include <chrono>
#include <thread>
#include <vector>

#include "tracy/Tracy.hpp"

// ---------------------------------------------------------------------------
// 破棄と初期化
// ---------------------------------------------------------------------------

JoltJobSystemAdapter::~JoltJobSystemAdapter()
{
    // 自分宛のジョブがキューに残っている間は待ってから壊れる
    // WaitForJobs が呼び出し元でジョブを実行すると、同じジョブを包んだラムダがキューに残る
    // そのラムダは最後に Release から FreeJob を通り、このアダプタのメンバを触るので、先に壊れると解放済みメモリを踏む
    // 今は EngineServices の宣言順で共有プールが先に壊れて残数は 0 だが、遠い宣言順に頼らず自分で確かめる
    while (m_queuedJobCount.load(std::memory_order_acquire) > 0)
    {
        std::this_thread::yield();
    }
}



void JoltJobSystemAdapter::Init(CCL::ECS::JobSystem *inPool, JPH::uint inMaxJobs, JPH::uint inMaxBarriers)
{
    JPH_ASSERT(inPool != nullptr, "JoltJobSystemAdapter needs a worker pool");

    m_pool = inPool;

    // バリアの実装は親（JobSystemWithBarrier）が持つ。フリーリストは Jolt 純正と同じく、1ページに最大数を収める
    JobSystemWithBarrier::Init(inMaxBarriers);
    m_jobs.Init(inMaxJobs, inMaxJobs);
}



int JoltJobSystemAdapter::GetMaxConcurrency() const
{
    // ワーカーの本数に、待っている呼び出し元のスレッドの分を足す（WaitForJobs は待つ間もジョブを実行する）
    // Jolt はこの数で仕事を割るので、大きく答えると細かく割りすぎる。Jolt 純正も mThreads.size() + 1 を返す
    if (m_pool == nullptr) return 1;
    return static_cast<int>(m_pool->GetWorkerCount()) + 1;
}



// ---------------------------------------------------------------------------
// ジョブの確保・投入・解放
// ---------------------------------------------------------------------------

JoltJobSystemAdapter::JobHandle JoltJobSystemAdapter::CreateJob(
    const char        *inName,
    JPH::ColorArg      inColor,
    const JobFunction &inJobFunction,
    JPH::uint32        inNumDependencies)
{
    // ---------------------------------------------------------
    // 0. Tracy の計装
    // ---------------------------------------------------------

    // ジョブは、ワーカーがキューから拾う経路と、バリア待ちのスレッドが Execute() を直接呼ぶ経路で走る
    // 後者は Jolt.lib の中で完結して QueueJob も RunJob も通らないので、RunJob に置くと計測から漏れる
    // どちらの経路も最後は mJobFunction() を呼ぶので、それを包めば必ずゾーンが張られる
    // std::function を1段挟むコストは計測用ビルドだけのもので、Shipping では素通しになる

    JPH::uint32 index;

#if defined(TRACY_ENABLE) && defined(JPH_PROFILE_ENABLED)

    // inName は Jolt 側の文字列リテラル（"StepBroadPhase" 等）で寿命は静的
    // ポインタをコピーして保持して問題ない
    JobFunction instrumented = [inName, fn = inJobFunction]() {
        ZoneTransientN(joltJobZone, inName, true);
        fn();
    };

    for (;;)
    {
        index = m_jobs.ConstructObject(inName, inColor, this, instrumented, inNumDependencies);
        if (index != AvailableJobs::cInvalidObjectIndex) break;

        JPH_ASSERT(false, "No jobs available! (cMaxPhysicsJobs が足りていない)");
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

#else

    // 1. フリーリストから枠を確保して Job を構築する
    // 枠が枯れたら Jolt 純正と同じく待って再試行する。枯れるのは cMaxPhysicsJobs の不足なので、アサートで気付く
    for (;;)
    {
        index = m_jobs.ConstructObject(inName, inColor, this, inJobFunction, inNumDependencies);
        if (index != AvailableJobs::cInvalidObjectIndex) break;

        JPH_ASSERT(false, "No jobs available! (cMaxPhysicsJobs が足りていない)");
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

#endif

    Job *job = &m_jobs.Get(index);

    // 2. ハンドルを作る（ここで参照カウントが1になる）
    JobHandle handle(job);

    // 3. 依存がないなら即実行可能なのでキューへ
    // 依存が残っていれば、0 になった時点で Jolt が QueueJob を呼ぶ
    if (inNumDependencies == 0)
    {
        QueueJob(job);
    }

    return handle;
}



void JoltJobSystemAdapter::QueueJob(Job *inJob)
{
    // Job はキューに入っている間しか生存が保証されないので、ラムダで持つ間は AddRef で延ばし、実行後に Release する
    inJob->AddRef();
    m_queuedJobCount.fetch_add(1, std::memory_order_relaxed);

    m_pool->Execute([this, inJob] { RunJob(inJob); });
}



void JoltJobSystemAdapter::QueueJobs(Job **inJobs, JPH::uint inNumJobs)
{
    // ExecuteBatch で1回のロックにまとめる。Jolt は1ステップで数百〜数千のジョブを積むので、ロックの回数が効く
    if (inNumJobs == 0) return;

    // ワーカーからも呼ばれるので、一時バッファは thread_local にして毎回の確保を避ける
    thread_local std::vector<CCL::ECS::Job> s_batch;
    s_batch.clear();
    s_batch.reserve(inNumJobs);

    m_queuedJobCount.fetch_add(static_cast<int>(inNumJobs), std::memory_order_relaxed);

    for (JPH::uint i = 0; i < inNumJobs; ++i)
    {
        Job *job = inJobs[i];
        job->AddRef();
        s_batch.emplace_back([this, job] { RunJob(job); });
    }

    m_pool->ExecuteBatch(s_batch.data(), s_batch.size());
}



void JoltJobSystemAdapter::FreeJob(Job *inJob)
{
    // 自分では呼ばない。Job::Release() で参照カウントが 0 になったとき、Jolt から呼ばれる
    m_jobs.DestructObject(inJob);
}



void JoltJobSystemAdapter::RunJob(Job *inJob)
{
    // Tracy のゾーンはここに置かない。バリア待ちのスレッドが直接実行する経路は、ここを通らない
    // 他のスレッドが実行済みのジョブを拾っても、Execute() が CAS で弾いてすぐ戻る
    inJob->Execute();

    // QueueJob / QueueJobs で AddRef した分を返す
    // これ以降 inJob を触ってはいけない（解放されている可能性がある）
    inJob->Release();

    // 「自分宛のラムダがキューから消えた」ことをデストラクタへ知らせる
    // release にしているのは、上の処理がすべて完了してからカウントが減るのを
    // 待ち側（acquire で読む）に保証するため。必ず最後に置くこと
    m_queuedJobCount.fetch_sub(1, std::memory_order_release);
}
