#pragma once
// ===========================================================================
// 概要: マルチスレッド処理用のスレッドプール JobSystem。
//
// 責務:
//   - 起動時に「論理コア数 - 1」本のワーカースレッドを作る
//   - 投げられたジョブ（引数・戻り値なしの関数）をキューに積み、並列に消化させる
//   - JobCounter による完了待ち（待つ側のスレッドもジョブを手伝う）
//
// 備考: 用語の対応
//   - Worker        : ジョブを実行するスレッド
//   - JobQueue      : 実行待ちのジョブを溜めるキュー
//   - Mutex         : キューへの同時アクセスを防ぐ鍵
//   - ConditionVar  : 寝ているワーカーを起こす通知
//   - JobCounter    : 投げたジョブの残り件数（0 になれば全完了）
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

    // 引数なし・戻り値なしの関数（ラムダ式など）。例:「Chunk0番を更新する」
    using Job = std::function<void()>;

    // 仕事の完了待ちをするためのカウンター。
    // 素の int では複数スレッドからの増減が競合して値が壊れるため、
    // 不可分に増減できる std::atomic を使う。
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

        /**
         * @brief ジョブをキューに追加する。
         * @param job     実行したい処理（ラムダ式など）
         * @param counter 完了を追跡するカウンター（nullptr なら追跡しない）
         */
        void Execute(Job job, JobCounter *counter = nullptr);

        /**
         * @brief 複数のジョブを「1回のロックで」まとめてキューに追加する。
         *
         * Execute() を N 回呼ぶと mutex の獲得・解放が N 回発生する。
         * 1回の投入が数百〜数千件になる用途（Jolt の物理ジョブなど）では
         * このロック競合そのものが並列化の利得を食い潰すため、
         * まとめて1回のロックで積むための入口を用意している。
         *
         * @param inJobs  実行したい処理の配列（中身はムーブされる）
         * @param inCount 件数
         * @param counter 完了を追跡するカウンター（nullptr なら追跡しない）
         */
        void ExecuteBatch(Job *inJobs, size_t inCount, JobCounter *counter = nullptr);

        /**
         * @brief 指定したカウンターが 0 になるまで、呼び出し元スレッドを待たせる。
         *
         * ただ待つのではなく、待っている間はキューからジョブを取って自分でも実行する。
         *
         * @param counter 監視対象のカウンター
         */
        void WaitForCounter(JobCounter *counter);

        /**
         * @brief 生成済みワーカースレッドの本数を返す。
         *
         * 「同時に実行できるジョブ数」はこれに +1（依頼側スレッドも
         * WaitForCounter の中で仕事を手伝うため）。
         * Jolt 側の GetMaxConcurrency() がこの値を必要とする。
         */
        unsigned int GetWorkerCount() const { return static_cast<unsigned int>(m_workers.size()); }

    private:
        /**
         * @brief ワーカースレッドの本体。生成から終了まで、このループでジョブを待って実行し続ける。
         *
         * workerIndex は Tracy 上のスレッド名（"ECS Worker 0" など）に使う。
         * 全員を同じ名前にすると、プロファイラ上でどのスレッドがどれか判別できず、
         * 物理（Jolt Worker N）との棲み分けも見えなくなるため通し番号を振る。
         */
        void WorkerLoop(unsigned int workerIndex);

    private:
        struct InternalJob
        {
            Job         task;    // 実行する処理
            JobCounter *counter; // 終わったら減らすカウンター（所有しない。nullptr 可）
        };

        std::vector<std::thread> m_workers;
        std::queue<InternalJob>  m_jobQueue;

        // m_jobQueue と m_shutdown を守る鍵。
        // これが無いと複数のワーカーが同時に同じジョブを取り出して壊れる。
        std::mutex m_queueMutex;

        std::condition_variable m_condition; // 寝ているワーカーを起こす通知

        bool m_shutdown = false; // true になったらワーカーはキューを空にしてから終了する
    };

} // namespace CCL::ECS
