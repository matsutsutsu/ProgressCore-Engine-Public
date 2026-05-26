/**
 * @file   CCL_JobSystem.cpp
 * @brief  JobSystemの実装
 *
 * @details
 *  スレッドプールの確立/解体、Jobキューの操作、Workerループの実装。
 *  WaitForCounterではメインスレッドもJobを手伝うワークスティーリング型です。
 *
 * @author CCL ECS Library
 */
#include "CCL_JobSystem.h"


namespace CCL::ECS::Core
{
    // =========================================================================
    //  ライフサイクル
    // =========================================================================

    JobSystem::JobSystem()
    {
        unsigned int coreCount = std::thread::hardware_concurrency();
        if (coreCount == 0) coreCount = 4; // フォールバック

        _workers.reserve(coreCount);
        for (unsigned int i = 0; i < coreCount; ++i) {
            _workers.emplace_back([this] { WorkerLoop(); });
        }
    }


    JobSystem::~JobSystem()
    {
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _shutdown = true;
        }
        _condition.notify_all();

        for (auto& w : _workers) {
            if (w.joinable()) w.join();
        }
    }


    // =========================================================================
    //  Execute / WaitForCounter
    // =========================================================================

    void JobSystem::Execute(Job job, JobCounter* counter)
    {
        // 先にカウンタを増やしておく
        // (ジョブが瞬時に完了して fetch_sub される前に store する競合を防ぐ)
        if (counter) {
            counter->count.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _jobQueue.push({std::move(job), counter});
        }

        _condition.notify_one();
    }


    void JobSystem::WaitForCounter(JobCounter* counter)
    {
        if (!counter) return;

        while (counter->Load() > 0)
        {
            InternalJob job;
            bool        foundJob = false;

            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                if (!_jobQueue.empty()) {
                    job = std::move(_jobQueue.front());
                    _jobQueue.pop();
                    foundJob = true;
                }
            }

            if (foundJob) {
                if (job.task) job.task();
                if (job.counter) {
                    job.counter->count.fetch_sub(1, std::memory_order_release);
                }
            }
            else {
                std::this_thread::yield();
            }
        }
    }


    // =========================================================================
    //  Worker
    // =========================================================================

    void JobSystem::WorkerLoop()
    {
        while (true)
        {
            InternalJob currentJob;

            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _condition.wait(lock, [this] {
                    return _shutdown || !_jobQueue.empty();
                });

                if (_shutdown && _jobQueue.empty()) return;

                currentJob = std::move(_jobQueue.front());
                _jobQueue.pop();
            }

            if (currentJob.task) currentJob.task();
            if (currentJob.counter) {
                currentJob.counter->count.fetch_sub(1, std::memory_order_release);
            }
        }
    }

} // namespace CCL::ECS::Core
