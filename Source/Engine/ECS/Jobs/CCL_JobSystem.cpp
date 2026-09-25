#include "CCL_JobSystem.h"

#include <cstdio> // snprintf（Tracy のスレッド名組み立て）

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{

    JobSystem::JobSystem()
    {
        // hardware_concurrency() は「取得できなければ 0 を返す」規格なので、
        // そのまま引き算するとアンダーフローする。必ず先に潰しておく。
        unsigned int hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4;

        // ワーカー数は「論理コア数 - 1」。
        //
        // なぜ「-1」なのか:
        //   WaitForCounter() は、ただ待つのではなく自分でもキューから仕事を取って
        //   実行する。つまり「依頼した側のスレッド（多くはメインスレッド）も、
        //   実質1本のワーカーとして働く」。
        //   ここでコア数ぶん作ると 総並列度 = コア数 + 1 になり、
        //   走れるコアより走りたいスレッドの方が多い状態（オーバーサブスクリプション）
        //   になる。OS が強制的にスレッドを切り替えるので、その分だけ純粋に損。
        //
        //   Jolt の JobSystemThreadPool も同じ理由で hardware_concurrency() - 1 を
        //   ワーカー数にしている（JoltPhysicsManager.cpp）。数え方を揃えておかないと、
        //   Tracy で両者のスレッド数を比較したときに理由のない差として見えてしまう。
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
        // 1. 終了フラグを立てる。
        //    ロックを取るのは、ワーカーが「フラグ確認」→「wait に入る」までの
        //    わずかな隙間にこの書き換えが割り込むと、ワーカーが終了に気付かないまま
        //    永遠に寝てしまう（通知の取りこぼし）ため。
        //    ロック中はワーカーが wait に入り切るまで書き換えが待たされる。
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
        // 1. カウンターはキューに積む「前に」増やす。
        //    積んでから増やすと、ワーカーが先に終えて減らしてしまい、
        //    カウンターが 0 → -1 になって完了待ちが壊れることがある。
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

        // 2. ロックは1回だけ。ここが Execute() を N 回呼ぶ場合との違い
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            for (size_t i = 0; i < inCount; ++i)
            {
                m_jobQueue.push({std::move(inJobs[i]), counter});
            }
        }

        // 3. 1件なら1本だけ起こせば十分。複数件なら全員起こす。
        //    件数ぶん notify_one を呼ぶと通知回数がそのまま増えてしまうため、
        //    複数件は notify_all 1回で済ませる。
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
                // キューは空だがカウンターが 0 でない ＝ 他のワーカーが実行中。
                // CPU を譲って待つ
                std::this_thread::yield();
            }
        }
    }

    void JobSystem::WorkerLoop(unsigned int workerIndex)
    {
        // Tracy のスレッド一覧に出る名前を付ける。
        //   全員 "Worker Thread" だと、プロファイラ上でどのスレッドが何本目か分からず
        //   「何本が実際に働いていて、何本が寝ているか」を読めない。通し番号を振る。
        //   物理側は "Jolt Worker N" と名付けてあるので（JoltPhysicsManager.cpp）、
        //   タイムライン上で ECS 並列と物理並列を目で分離できる。
        //
        //   なお tracy::SetThreadName はマクロではなく実体のある関数で、
        //   TracyClient.cpp が TRACY_ENABLE の有無に関わらずコンパイルするため
        //   Shipping ビルドでもリンクできる（中身は何もしない）。
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

                // 「ジョブが来る」か「終了」まで寝て待つ。
                // wait は待機に入る際に lock を解放し、起床時に再取得してから戻る。
                // そのため待っている間、他スレッドはキューを触れる。
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

} // namespace CCL::ECS
