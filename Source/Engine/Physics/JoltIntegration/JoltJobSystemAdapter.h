#pragma once
// ===========================================================================
// 概要: Jolt Physics のジョブを、エンジン側のスレッドプール（CCL::ECS::JobSystem）の
//       上で実行させるためのアダプタ。
//
// 責務:
//   - JPH::JobSystem の実装（ジョブの確保・キューへの投入・解放）
//   - 各ジョブの関数を包み、Tracy にジョブ名付きのゾーンを出す
//   - 破棄時に、共有プールへ積んだ自分宛のジョブが残っていないことを確認する
//
// なぜ必要か:
//   Jolt は既定で JPH::JobSystemThreadPool という独自スレッドプールを持つ。
//   そのままだと 1台のPCの中に
//       ・ECS のフレーム並列用プール（自作）
//       ・Jolt の物理用プール（Jolt製）
//   という2つのスレッドプールが同時に常駐し、次の2つの問題が出る。
//
//     1) スレッド数の二重確保
//        16スレッドのCPUで「ECS 15本 + Jolt 15本 + メイン1本」= 31本になる。
//        走れるコアより走りたいスレッドが多い状態（オーバーサブスクリプション）で、
//        OSのコンテキストスイッチぶんだけ純粋に損をする。
//
//     2) 物理の負荷が Tracy で見えない
//        Tracy がタイムラインに行を作るのは「イベントを送信したスレッド」だけ。
//        Jolt のジョブ関数の中には Tracy のゾーンが1つも無いため、
//        スレッド名を付けても物理ワーカーの行は一切表示されない。
//        ジョブ単位で可視化するには「ジョブの実行が自分のコードを通る」必要がある。
//
//   このアダプタを挟むと、ジョブの生成（CreateJob）がこちらを通るので、
//   ジョブ関数を包んで Tracy ゾーンを張れる。プールも1本に統合される。
//
// Jolt 側の想定:
//   JPH::JobSystem は抽象インターフェースで、JobSystemThreadPool は
//   「単なる既定実装」に過ぎない（JobSystem.h のコメントに明記されている）。
//   バリア（ジョブ同士の待ち合わせ）の実装は JPH::JobSystemWithBarrier を
//   継承すれば貰えるので、こちらで実装するのは実質4関数だけで済む。
//     - GetMaxConcurrency
//     - CreateJob
//     - QueueJob / QueueJobs
//     - FreeJob
//
// デッドロックしないのか:
//   Jolt の WaitForJobs は「待つだけ」ではなく、待っている間に自分の
//   バリアに属するジョブを呼び出し元スレッド上で実行する。
//   そのため、仮に他のワーカーが全員別の仕事で埋まっていても、
//   待っているスレッド自身が物理ジョブを消化して前に進める。
//
//   なお同じジョブがこちらのキューにも積まれたままになるが、
//   JPH::JobSystem::Job::Execute() は先頭で CAS により実行中状態へ遷移させ、
//   二重実行を弾く。後から拾ったワーカーは即座に何もせず戻る（安全）。
// ===========================================================================

// Jolt の基本ヘッダ（他の Jolt ヘッダより先に）
#include <Jolt/Jolt.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/FixedSizeFreeList.h>

#include <atomic>

// 前方宣言（このヘッダはECS側の実装詳細に依存しない）
namespace CCL::ECS
{
    class JobSystem;
}

class JoltJobSystemAdapter final : public JPH::JobSystemWithBarrier
{
public:
    JoltJobSystemAdapter() = default;

    // 積んだジョブが全て消化されるのを待ってから破棄する（詳細は .cpp）
    ~JoltJobSystemAdapter() override;

    /**
     * @brief アダプタを初期化する。
     *
     * @param inPool        実際にジョブを走らせる自作スレッドプール（所有しない）。
     *                      EngineServices が持つ唯一の共有プールを渡す想定。
     * @param inMaxJobs     同時に確保しうるジョブ数の上限。Jolt が要求する値を渡す。
     * @param inMaxBarriers 同時に確保しうるバリア数の上限。
     */
    void Init(CCL::ECS::JobSystem *inPool, JPH::uint inMaxJobs, JPH::uint inMaxBarriers);

    // ============================================================
    // JPH::JobSystem の実装
    // ============================================================

    // 同時に実行できるジョブ数を返す（Jolt が仕事の分割数を決めるのに使う）
    int GetMaxConcurrency() const override;

    // ジョブを1つ確保する。依存が0なら即座にキューへ積まれる。
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
    // ワーカースレッド上でジョブを実行し、キューに積んだときの参照を返す。
    // Tracy ゾーンはここではなく CreateJob で張る（理由は .cpp）。
    void RunJob(Job *inJob);

    // Jobの実体を置くフリーリスト。Jolt純正実装と同じ方式。
    // 毎フレーム数百〜数千個 new/delete しないための固定プール。
    using AvailableJobs = JPH::FixedSizeFreeList<Job>;
    AvailableJobs m_jobs;

    // 実行を委譲する自作スレッドプール（所有しない）
    CCL::ECS::JobSystem *m_pool = nullptr;

    // 共有プールへ積んだが、まだ RunJob を抜けていないジョブの数。
    //
    // 破棄時に「自分宛のラムダがキューに残っていないか」を判定するために使う。
    // 詳しい理由は .cpp のデストラクタのコメントを参照。
    std::atomic<int> m_queuedJobCount{0};
};
