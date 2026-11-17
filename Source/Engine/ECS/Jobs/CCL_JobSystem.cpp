#include "CCL_JobSystem.h"

#include <cstdio>

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{

    JobSystem::JobSystem()
    {
        // 取れないと 0 が返るので、引き算の前に潰す
        unsigned int hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4;

        // ワーカーは論理コア数 - 1。WaitForCounter で待つ側のスレッドもジョブを実行するので、
        // コア数ぶん作ると、走るスレッドがコアより多くなり、OS の切り替えの分だけ遅くなる
        // Jolt のスレッドプールも同じ数え方にしている
        const unsigned int workerCount = (hwThreads > 1) ? (hwThreads - 1) : 1;

        // emplace_back した瞬間にスレッドが生成され、WorkerLoop() が別スレッドで動き出す
        m_workers.reserve(workerCount);
        for (unsigned int i = 0; i < workerCount; ++i)
        {
            m_workers.emplace_back([this, i] { WorkerLoop(i); });
        }
    }



    JobSystem::~JobSystem()
    {
        // 1. 終了フラグを立てる
        // 1. 終了フラグを立てる
        //    ロックを取らないと、ワーカーがフラグを見てから wait に入るまでの間に書き換わり、
        //    終了に気付かないまま寝続ける
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_shutdown = true;
        }

        // 2. 寝ているワーカーを全員起こす。起きたワーカーは m_shutdown を見て終了する
        m_condition.notify_all();

        // 3. 全ワーカーのスレッドが終了するまで待つ
        for (std::thread &worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }



    void JobSystem::Execute(Job job, JobCounter *counter)
    {
        // 1. カウンターはキューに積む前に増やす
        //    後にすると、ワーカーが先に終えて減らし、カウンターが -1 になって完了待ちが壊れる
        if (counter)
        {
            counter->count.fetch_add(1);
        }

        // 2. キューに積む（スコープを抜けるとロック解除）
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_jobQueue.push({std::move(job), counter});
        }

        // 3. 寝ているワーカーを1本だけ起こす
        m_condition.notify_one();
    }



    void JobSystem::ExecuteBatch(Job *inJobs, size_t inCount, JobCounter *counter)
    {
        if (inCount == 0) return;

        // 1. カウンターはまとめて先に増やす（理由は Execute と同じ）
        if (counter)
        {
            counter->count.fetch_add(static_cast<int>(inCount));
        }

        // 2. N 件をロック1回で積む
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            for (size_t i = 0; i < inCount; ++i)
            {
                m_jobQueue.push({std::move(inJobs[i]), counter});
            }
        }

        // 3. 1件なら1本だけ起こし、複数件なら notify_all の1回で全員起こす
        if (inCount == 1)
        {
            m_condition.notify_one();
        }
        else
        {
            m_condition.notify_all();
        }
    }



    void JobSystem::WaitForCounter(JobCounter *counter)
    {
        if (!counter) return;

        while (counter->count.load() > 0)
        {
            // ただ待たずに、キューに残っているジョブを自分でも実行して手伝う
            InternalJob job;
            bool        foundJob = false;

            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                if (!m_jobQueue.empty())
                {
                    job = std::move(m_jobQueue.front());
                    m_jobQueue.pop();
                    foundJob = true;
                }
            }

            if (foundJob)
            {
                if (job.task)
                {
                    job.task();
                }
                if (job.counter)
                {
                    job.counter->count.fetch_sub(1);
                }
            }
            else
            {
                // キューは空だが、ほかのワーカーがまだ実行中なので CPU を譲って待つ
                std::this_thread::yield();
            }
        }
    }



    void JobSystem::WorkerLoop(unsigned int workerIndex)
    {
        // Tracy のスレッド一覧で何本目か分かるよう、通し番号の名前を付ける
        // tracy::SetThreadName は関数なので、Shipping でもリンクできる（中身は空）
        char threadName[32];
        snprintf(threadName, sizeof(threadName), "ECS Worker %u", workerIndex);
        tracy::SetThreadName(threadName);

        // 終了フラグが立つまでジョブを待って実行し続ける
        while (true)
        {
            InternalJob currentJob;

            // キューを触る区間だけロックする
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);

                // ジョブが来るか終了するまで寝て待つ。待っている間はロックが外れる
                m_condition.wait(lock, [this] { return m_shutdown || !m_jobQueue.empty(); });

                // 終了フラグが立っていても、残っているジョブは消化してから抜ける
                if (m_shutdown && m_jobQueue.empty())
                {
                    return;
                }

                currentJob = std::move(m_jobQueue.front());
                m_jobQueue.pop();
            }
            // ここでロック解除。以降の実行は全ワーカーが同時に進められる

            if (currentJob.task)
            {
                currentJob.task();
            }

            // 0 になると、WaitForCounter で待っているスレッドが解放される
            if (currentJob.counter)
            {
                currentJob.counter->count.fetch_sub(1);
            }
        }
    }

}
