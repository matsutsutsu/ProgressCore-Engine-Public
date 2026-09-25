#include "JoltJobSystemAdapter.h"

#include "Engine/ECS/Jobs/CCL_JobSystem.h"

#include <chrono>
#include <thread>
#include <vector>

#include "tracy/Tracy.hpp"

// ============================================================================
// デストラクタ ─ 自分宛のジョブが残っていないことを確認してから壊れる
// ----------------------------------------------------------------------------
//  一言で : 「まだキューに自分のラムダが残っている状態」で死なないための保険。
//
//  なぜ必要か
//    Jolt の WaitForJobs は待機中に呼び出し元スレッドでもジョブを実行する。
//    その場合、同じジョブを包んだラムダは共有プールのキューに残ったままになる
//    （後から拾ったワーカーは Execute() の CAS で弾かれて何もせず戻る）。
//    このラムダは最後に inJob->Release() を呼び、参照カウントが0になると
//    FreeJob() → m_jobs.DestructObject() と、このアダプタのメンバを触る。
//    つまりアダプタが先に壊れていると解放済みメモリを踏む。
//
//  現状の破棄順について
//    EngineServices のメンバ宣言順（jobSystem が physicsManager より後）により、
//    実際には共有プールが先に破棄される。プールのデストラクタはキューを
//    空にしてからワーカーを join するので、この時点で残数は 0 になっている。
//    つまり今は「たまたま安全」。
//    その安全性がヘッダの宣言順という遠い場所に依存しているのは危ういので、
//    アダプタ自身が自分で確認する形にしてある。
//
//  ここが無限ループにならない理由
//    ワーカーが既に停止しているなら、プールのデストラクタがキューを
//    drain し終わった後なので残数は必ず 0。
//    ワーカーが生きているなら、残っているラムダは順次消化される。
// ============================================================================
JoltJobSystemAdapter::~JoltJobSystemAdapter()
{
    while (m_queuedJobCount.load(std::memory_order_acquire) > 0)
    {
        std::this_thread::yield();
    }
}

// ============================================================================
// Init ─ フリーリストとバリアを用意し、委譲先プールを覚える
// ----------------------------------------------------------------------------
//  一言で : 「ジョブ置き場」と「待ち合わせ場」を先に確保しておく初期化。
//
//  JobSystemWithBarrier::Init(maxBarriers) が親側のバリア実装を初期化する。
//  m_jobs.Init(maxJobs, maxJobs) は Jolt 純正の JobSystemThreadPool と
//  同じ引数の与え方（ページサイズ = 最大数 = 1ページで収める）。
// ============================================================================
void JoltJobSystemAdapter::Init(CCL::ECS::JobSystem *inPool, JPH::uint inMaxJobs, JPH::uint inMaxBarriers)
{
    JPH_ASSERT(inPool != nullptr, "JoltJobSystemAdapter needs a worker pool");

    m_pool = inPool;

    JobSystemWithBarrier::Init(inMaxBarriers);
    m_jobs.Init(inMaxJobs, inMaxJobs);
}

// ============================================================================
// GetMaxConcurrency ─ 同時に走れるジョブ数
// ----------------------------------------------------------------------------
//  一言で : ワーカー本数 + 1。
//
//  なぜ +1 なのか
//    Jolt の WaitForJobs は待機中に呼び出し元スレッド自身もジョブを実行する。
//    つまり「待っているスレッド」も1人の働き手として数える必要がある。
//    Jolt 純正の JobSystemThreadPool も同じく mThreads.size() + 1 を返す。
//
//  この値は Jolt が仕事の分割数（バッチ数）を決めるのに使う。
//  実際より大きく答えると細かく割りすぎてオーバーヘッドが増える。
// ============================================================================
int JoltJobSystemAdapter::GetMaxConcurrency() const
{
    if (m_pool == nullptr) return 1;
    return static_cast<int>(m_pool->GetWorkerCount()) + 1;
}

// ============================================================================
// CreateJob ─ ジョブを1つ確保する
// ----------------------------------------------------------------------------
//  一言で : フリーリストから1枠取って Job を構築し、ハンドルを返す。
//
//  依存カウントが 0 のジョブは「今すぐ走れる」ので、その場でキューへ積む。
//  0 でなければ、他のジョブが RemoveDependency() で 0 にした時点で
//  Jolt 内部から QueueJob() が呼ばれる（＝こちらから積む必要はない）。
//
//  枠が枯れた場合について
//    Jolt 純正実装と同じく、少し待って再試行する。ここで諦めると
//    物理シミュレーションが不整合なまま進んでしまうため、
//    「取れるまで粘る」のが正しい振る舞い。
//    そもそも枯れるのは cMaxPhysicsJobs の設定不足なので、
//    アサートで気付けるようにしてある。
// ============================================================================
JoltJobSystemAdapter::JobHandle JoltJobSystemAdapter::CreateJob(
    const char        *inName,
    JPH::ColorArg      inColor,
    const JobFunction &inJobFunction,
    JPH::uint32        inNumDependencies)
{
    // ------------------------------------------------------------------
    // 0. Tracy 計装は「ジョブ関数そのもの」を包む形で行う
    // ------------------------------------------------------------------
    //
    //  なぜ QueueJob / RunJob 側ではダメだったのか（重要）
    //    Jolt のジョブが実行される経路は2つある。
    //
    //      経路A: ワーカースレッドがキューから拾う
    //             → QueueJob で積んだラムダ → RunJob() → Execute()
    //
    //      経路B: バリア待ちのスレッドが自分で実行する
    //             → JobSystemWithBarrier::WaitForJobs() が
    //               Job::Execute() を直接呼ぶ
    //
    //    経路B はプリビルドの Jolt.lib の中で完結していて、こちらの
    //    QueueJob も RunJob も一切通らない。しかも BarrierImpl は
    //    JobSystemWithBarrier の private クラスなので手を入れられない。
    //    そして物理は「投げてすぐ待つ」ため、経路B で消化される割合が高い。
    //
    //    RunJob にゾーンを置くと、この経路B のジョブがまるごと計測から漏れる
    //    （Tracy に何も出ない）。
    //
    //  なぜここなら全部拾えるのか
    //    どちらの経路も最終的に Job::Execute() → mJobFunction() を呼ぶ。
    //    その mJobFunction を差し替えてしまえば、実行スレッドが何であれ
    //    必ずゾーンが張られる。
    //
    //  コストについて
    //    std::function を1段挟むぶんの間接呼び出しと、ZoneTransientN による
    //    ソースロケーションの実行時確保が乗る。計測用ビルド専用の負荷なので
    //    許容している（Shipping では #else 側に落ちて素通し）。
    // ------------------------------------------------------------------

    JPH::uint32 index;

#if defined(TRACY_ENABLE) && defined(JPH_PROFILE_ENABLED)

    // inName は Jolt 側の文字列リテラル（"StepBroadPhase" 等）で寿命は静的。
    // ポインタをコピーして保持して問題ない。
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

    // 3. 依存が無いなら即実行可能なのでキューへ
    if (inNumDependencies == 0)
    {
        QueueJob(job);
    }

    return handle;
}

// ============================================================================
// QueueJob ─ 実行可能になったジョブを1件積む
// ----------------------------------------------------------------------------
//  一言で : Job* をラムダに包んで自作プールへ渡すだけ。
//
//  AddRef() が要る理由
//    JobSystem.h のコメントに明記されている通り、Job はキューに入れている
//    「間だけ」生存が保証される。自分のデータ構造（＝ここではラムダ）に
//    保持するなら AddRef() して寿命を延ばし、実行し終わったら Release() する。
//    これを忘れると、実行前にジョブが解放されて解放済みメモリを踏む。
// ============================================================================
void JoltJobSystemAdapter::QueueJob(Job *inJob)
{
    inJob->AddRef();
    m_queuedJobCount.fetch_add(1, std::memory_order_relaxed);

    m_pool->Execute([this, inJob] { RunJob(inJob); });
}

// ============================================================================
// QueueJobs ─ 実行可能になったジョブをまとめて積む
// ----------------------------------------------------------------------------
//  一言で : QueueJob の複数版。ロック回数を減らすために ExecuteBatch を使う。
//
//  QueueJob をN回呼んでも動作は同じだが、その場合キューの mutex を
//  N回獲得・解放することになる。Jolt は1ステップで数百〜数千のジョブを
//  投げるため、この回数差がそのままロック競合として効いてくる。
//
//  一時バッファを thread_local にしている理由
//    この関数はワーカースレッドからも呼ばれる（ジョブの中から次のジョブが
//    積まれるため）。関数ローカルの vector にすると毎回ヒープ確保が走るので、
//    スレッドごとに1本持って使い回す。
// ============================================================================
void JoltJobSystemAdapter::QueueJobs(Job **inJobs, JPH::uint inNumJobs)
{
    if (inNumJobs == 0) return;

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

// ============================================================================
// FreeJob ─ ジョブの後始末
// ----------------------------------------------------------------------------
//  一言で : フリーリストへ枠を返す。
//
//  自分で呼ぶ関数ではない。Job::Release() で参照カウントが 0 になったときに
//  Jolt 側から呼ばれる。
// ============================================================================
void JoltJobSystemAdapter::FreeJob(Job *inJob)
{
    m_jobs.DestructObject(inJob);
}

// ============================================================================
// RunJob ─ ワーカースレッド上での実際の実行
// ----------------------------------------------------------------------------
//  一言で : Execute() して Release() するだけ。
//
//  Tracy のゾーンはここには置かない。
//  バリア待ちのスレッドが Job::Execute() を直接呼ぶ経路（Jolt.lib 内部）は
//  この関数を通らないため、ここに置くと計測漏れが出る。
//  計装は CreateJob() でジョブ関数自体を包む形にしてある（そちらのコメント参照）。
//
//  なお、既に他スレッドが実行済みのジョブをここで拾うことがある。
//  その場合 Execute() は内部の CAS に弾かれて即座に戻るので、
//  二重実行の心配はいらない（判定を書く必要もない）。
// ============================================================================
void JoltJobSystemAdapter::RunJob(Job *inJob)
{
    inJob->Execute();

    // QueueJob / QueueJobs で AddRef した分を返す。
    // これ以降 inJob を触ってはいけない（解放されている可能性がある）。
    inJob->Release();

    // 「自分宛のラムダがキューから消えた」ことをデストラクタへ知らせる。
    // release にしているのは、上の処理が全て完了してからカウントが減るのを
    // 待ち側（acquire で読む）に保証するため。必ず最後に置くこと。
    m_queuedJobCount.fetch_sub(1, std::memory_order_release);
}
