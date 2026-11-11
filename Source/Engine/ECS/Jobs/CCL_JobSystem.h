#pragma once
// ===========================================================================
// 概要: ワーカースレッドでジョブを並列に実行するスレッドプール
//
// 責務:
//   - 起動時に「論理コア数 - 1」本のワーカーを作る
//   - 積まれたジョブ（引数・戻り値なしの関数）を並列に消化する
//   - JobCounter で完了を待つ。待つ側のスレッドもジョブを手伝う
// ===========================================================================
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace CCL::ECS
{

    // 引数・戻り値なしの関数（ラムダ式など）
    using Job = std::function<void()>;

    // 仕事の完了待ちをするためのカウンター
    // 素の int では複数スレッドからの増減が競合して値が壊れるため、
    // 不可分に増減できる std::atomic を使う
    struct JobCounter
    {
        std::atomic<int> count{0};
    };

    class JobSystem
    {
    public:
        // ワーカースレッドを生成して待機させる
        JobSystem();

        // 全ワーカーに終了を通知し、終わるまで待つ
        ~JobSystem();

        // プールはエンジンに1つだけ（EngineServices が持つ）なのでコピー禁止
        JobSystem(const JobSystem &)            = delete;
        JobSystem &operator=(const JobSystem &) = delete;

        // ジョブをキューに積む。counter を渡すと完了を数える
        void Execute(Job job, JobCounter *counter = nullptr);

        // 複数のジョブをロック1回でまとめて積む（中身はムーブする）
        // 数百〜数千件を一度に積む用途（Jolt の物理ジョブなど）では、Execute を N 回呼ぶとロックの競合で並列の利得が消える
        void ExecuteBatch(Job *inJobs, size_t inCount, JobCounter *counter = nullptr);

        // counter が 0 になるまで待つ。待つ間はキューからジョブを取って自分でも実行する
        void WaitForCounter(JobCounter *counter);

        // ワーカーの本数。待つ側のスレッドも手伝うので、同時に走るジョブはこれ + 1（Jolt の GetMaxConcurrency が使う）
        unsigned int GetWorkerCount() const { return static_cast<unsigned int>(m_workers.size()); }

    private:
        // ワーカースレッドの本体。workerIndex は Tracy に出すスレッド名の番号
        void WorkerLoop(unsigned int workerIndex);

    private:
        struct InternalJob
        {
            Job         task;    // 実行する処理
            JobCounter *counter; // 終わったら減らすカウンター（所有しない。nullptr 可）
        };

        std::vector<std::thread> m_workers;
        std::queue<InternalJob>  m_jobQueue;

        // m_jobQueue と m_shutdown を守る鍵
        // これがないと複数のワーカーが同時に同じジョブを取り出して壊れる
        std::mutex m_queueMutex;

        std::condition_variable m_condition; // 寝ているワーカーを起こす通知

        bool m_shutdown = false; // true になったらワーカーはキューを空にしてから終了する
    };

} // namespace CCL::ECS
