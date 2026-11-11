#pragma once
// ===========================================================================
// 概要: Jolt Physics のジョブを、エンジンのスレッドプール（CCL::ECS::JobSystem）の上で実行させるアダプタ
//
// 責務:
//   - JPH::JobSystem の実装（ジョブの確保・キューへの投入・解放）
//   - 各ジョブの関数を包み、Tracy にジョブ名付きのゾーンを出す
//   - 破棄時に、共有プールへ積んだ自分宛のジョブが残っていないことを確かめる
//
// 備考: Jolt 既定の JobSystemThreadPool だと ECS 用と物理用の2つのプールが常駐し、スレッドが倍になる
//       Jolt のジョブには Tracy のゾーンもないので、ここでジョブを包み、プールを1本にまとめる
//       WaitForJobs は待つ間に呼び出し元でもジョブを実行するので、ワーカーが埋まっていてもデッドロックしない
//       同じジョブがキューにも残るが、Job::Execute が CAS で二重の実行を弾く
// ===========================================================================

// Jolt の基本ヘッダ（他の Jolt ヘッダより先に）
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

#include <atomic>

// 前方宣言。このヘッダは ECS 側の実装に依存しない
namespace CCL::ECS
{
    class JobSystem;
}

class JoltJobSystemAdapter final : public JPH::JobSystemWithBarrier
{
public:
    JoltJobSystemAdapter() = default;

    // 積んだジョブがすべて消化されるのを待ってから破棄する（詳細は .cpp）
    ~JoltJobSystemAdapter() override;

    // アダプタを初期化する。inPool は実際にジョブを走らせるプールで、所有しない（EngineServices の共有プール）
    // inMaxJobs / inMaxBarriers は、同時に確保しうるジョブとバリアの上限。Jolt が要求する値を渡す
    void Init(CCL::ECS::JobSystem *inPool, JPH::uint inMaxJobs, JPH::uint inMaxBarriers);

    // ============================================================
    // JPH::JobSystem の実装
    // ============================================================

    // 同時に実行できるジョブ数を返す（Jolt が仕事の分割数を決めるのに使う）
    int GetMaxConcurrency() const override;

    // ジョブを1つ確保する。依存が0なら即座にキューへ積まれる
    JobHandle CreateJob(const char        *inName,
                        JPH::ColorArg      inColor,
                        const JobFunction &inJobFunction,
                        JPH::uint32        inNumDependencies = 0) override;

protected:
    // 実行可能になったジョブを1件キューへ積む
    void QueueJob(Job *inJob) override;

    // 実行可能になったジョブを複数まとめてキューへ積む
    void QueueJobs(Job **inJobs, JPH::uint inNumJobs) override;

    // ジョブの後始末（参照カウントが0になったとき Jolt から呼ばれる）
    void FreeJob(Job *inJob) override;

private:
    // ワーカースレッドでジョブを実行し、キューに積んだときに取った参照を Release で返す
    // Release の後は解放されているかもしれないので触らない。Tracy のゾーンはここでなく CreateJob で張る
    void RunJob(Job *inJob);

    // Job の実体を置くフリーリスト（Jolt 純正と同じ方式）。毎フレーム数百〜数千個を new / delete しない
    using AvailableJobs = JPH::FixedSizeFreeList<Job>;
    AvailableJobs m_jobs;

    // 実行を委譲する自作スレッドプール（所有しない）
    CCL::ECS::JobSystem *m_pool = nullptr;

    // 共有プールへ積んだが、まだ RunJob を抜けていないジョブの数。破棄時に自分宛のジョブが残っていないかを見る
    std::atomic<int> m_queuedJobCount{0};
};
