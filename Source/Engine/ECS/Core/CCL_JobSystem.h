/**
 * @file   CCL_JobSystem.h
 * @brief  並列タスク実行用のスレッドプール
 *
 * @details
 *  Logic/Render System の並列化に使われる汎用ジョブシステムです。
 *  内部でスレッドプールを保持し、Execute() で投入されたジョブを
 *  空きスレッドで並列実行します。
 *
 *  WaitForCounter() で「特定のジョブ群が全て終わるまで待機」できます。
 *  待機中はメインスレッドも仕事を手伝うため、CPUが遊びにくくなります。
 *
 *  【スレッドセーフ性】
 *   - Execute は任意スレッドから呼べる
 *   - WaitForCounter も任意スレッドから呼べる(ワークスティーリング)
 *
 *  【依存】
 *   - 標準スレッド/同期ライブラリのみ
 *
 * @author CCL ECS Library
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>


namespace CCL::ECS::Core
{
    /// @brief Job本体は「引数なし戻り値なし」の callable
    using Job = std::function<void()>;


    // =========================================================================
    //  JobCounter
    // =========================================================================

    /**
     * @struct JobCounter
     * @brief 投入された未完了ジョブ数を atomic で追跡するカウンタ
     */
    struct JobCounter
    {
        std::atomic<int> count{0};

        /// @brief カウンタ値を取得(リラックスメモリオーダ)
        int Load() const noexcept { return count.load(std::memory_order_relaxed); }
    };


    // =========================================================================
    //  JobSystem
    // =========================================================================

    /**
     * @class JobSystem
     * @brief Workerスレッドプール
     *
     * @details
     *  コンストラクタで std::thread::hardware_concurrency() 分のスレッドを生成し、
     *  Job投入を待ち受けます。デストラクタで全Workerを終了させます。
     */
    class JobSystem
    {
    public:
        JobSystem();
        ~JobSystem();

        JobSystem(const JobSystem&)            = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        /**
         * @brief Jobをキューに投入する
         * @param job     実行する処理
         * @param counter 完了を追跡するカウンタ(nullptr可)
         */
        void Execute(Job job, JobCounter* counter = nullptr);

        /**
         * @brief 指定カウンタが0になるまで待機する(同期点)
         * @param counter 監視カウンタ
         * @details 待機中もキューにJobがあれば呼び出し元スレッドが手伝います。
         */
        void WaitForCounter(JobCounter* counter);

        /// @brief Worker数(コア数 = ハードウェア並列度)
        std::size_t GetWorkerCount() const noexcept { return _workers.size(); }

    private:
        /// @brief Workerスレッドのループ本体
        void WorkerLoop();

        /// @brief 内部用Job構造体
        struct InternalJob
        {
            Job         task;
            JobCounter* counter = nullptr;
        };

        std::vector<std::thread> _workers;       ///< Workerスレッド
        std::queue<InternalJob>  _jobQueue;      ///< Jobキュー
        std::mutex               _queueMutex;    ///< キュー保護
        std::condition_variable  _condition;     ///< Worker起床通知
        bool                     _shutdown = false; ///< シャットダウンフラグ
    };

} // namespace CCL::ECS::Core
