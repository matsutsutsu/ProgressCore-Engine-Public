#include "CCL_JobSystem.h"

#include <cstdio> // snprintf（Tracy のスレッド名組み立て）

#include "tracy/Tracy.hpp"

namespace CCL::ECS
{

    // =======================================================================
    // 開店準備（コンストラクタ）
    // =======================================================================
    JobSystem::JobSystem()
    {
        // 1. パソコンの脳みそ（論理コア数）を調べる
        //    hardware_concurrency() は「取得できなければ 0 を返す」規格なので、
        //    そのまま引き算するとアンダーフローする。必ず先に潰しておく。
        unsigned int hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 4; // 分からなければとりあえず4人雇う

        // 2. 雇うシェフの人数を決める（コア数 - 1）
        //
        // なぜ「-1」なのか:
        //   WaitForCounter() は、ただ待つのではなく自分でもキューから仕事を取って
        //   実行する（下の実装を参照）。つまり「依頼した側のスレッド（多くはメイン
        //   スレッド）も、実質1人のシェフとして厨房に入る」。
        //   ここでコア数フルに雇うと 総並列度 = コア数 + 1 になり、
        //   走れるコアより走りたいスレッドの方が多い状態（オーバーサブスクリプション）
        //   になる。OSが強制的にスレッドを切り替えるので、その分だけ純粋に損。
        //
        //   Jolt の JobSystemThreadPool も同じ理由で hardware_concurrency() - 1 を
        //   ワーカー数にしている（JoltPhysicsManager.cpp）。数え方を揃えておかないと、
        //   Tracy で両者のスレッド数を比較したときに理由のない差として見えてしまう。
        const unsigned int workerCount = (hwThreads > 1) ? (hwThreads - 1) : 1;

        // 3. 人数分のシェフ（スレッド）を作成
        // m_workers.emplace_back した瞬間に、スレッドが生成され
        // WorkerLoop() という関数が別世界（別スレッド）で動き出します。
        m_workers.reserve(workerCount);
        for (unsigned int i = 0; i < workerCount; ++i)
        {
            m_workers.emplace_back([this, i] { WorkerLoop(i); });
        }
    }

    // =======================================================================
    // 閉店作業（デストラクタ）
    // =======================================================================
    JobSystem::~JobSystem()
    {
        // 1. 「もう閉店ですよ」という看板（フラグ）を立てる
        // 重要: ここで Mutex ロックをする理由
        // シェフが「フラグ確認」→「寝る準備」→「実際に寝る」までのわずかな隙間に
        // この書き換えが発生すると、シェフが「閉店に気づかずに永遠に寝てしまう」事故が起きるため。
        // ロックすることで「シェフが寝る準備を完了するまで」待ってから書き換える。
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_shutdown = true;
        }

        // 2. 非常ベルを鳴らして全員叩き起こす
        // 寝ているシェフ全員が `wait` から目覚め、`m_shutdown` フラグを見て帰宅準備に入る。
        m_condition.notify_all();

        // 3. 全員が更衣室から出てくる（スレッドが終了する）のを待つ
        for (std::thread &worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join(); // シェフが完全に帰るまでここで待機
            }
        }
    }

    // =======================================================================
    // 仕事の依頼（Execute）
    // =======================================================================
    void JobSystem::Execute(Job job, JobCounter *counter)
    {
        // 1. カウンターを「先に」増やしておく（超重要）
        // なぜ？：もし「キューに入れてから」増やすと、シェフが爆速すぎて
        // 「店長が増やす前に、シェフが終わらせて減らしてしまう」可能性があるから。
        // 0 -> -1 になってバグるのを防ぐため、仕事を入れる前に「予約」として増やしておく。
        if (counter)
        {
            counter->count.fetch_add(1);
        }

        {
            // 2. 鍵をかけてボード（キュー）を独占する
            std::unique_lock<std::mutex> lock(m_queueMutex);

            // 仕事のボードにjob（システム処理function）を貼り付ける
            m_jobQueue.push({std::move(job), counter});
        }
        // ここでロック解除（スコープ抜け）

        // 3. 休憩室にベルを鳴らす。「1人起きてー！」
        // 寝ているシェフの中から1人だけを起こす。
        m_condition.notify_one();
    }

    // =======================================================================
    // まとめて依頼（ExecuteBatch）
    // =======================================================================
    void JobSystem::ExecuteBatch(Job *inJobs, size_t inCount, JobCounter *counter)
    {
        if (inCount == 0) return;

        // 1. カウンターは「まとめて先に」増やす（理由は Execute と同じ）
        if (counter)
        {
            counter->count.fetch_add(static_cast<int>(inCount));
        }

        {
            // 2. ロックは1回だけ。ここが Execute() をN回呼ぶ場合との違い。
            std::unique_lock<std::mutex> lock(m_queueMutex);
            for (size_t i = 0; i < inCount; ++i)
            {
                m_jobQueue.push({std::move(inJobs[i]), counter});
            }
        }

        // 3. 起こし方
        //    1件なら1人だけ起こせば十分。複数件なら全員起こす。
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

    // =======================================================================
    // 完了待ち（WaitForCounter）
    // =======================================================================
    void JobSystem::WaitForCounter(JobCounter *counter)
    {
        if (!counter) return;

        // カウンターが0になるまでループ
        while (counter->count.load() > 0)
        {

            // --- 待ってるだけじゃなくて手伝う！ ---
            InternalJob job;
            bool        foundJob = false;

            {
                // ロックして仕事があるか確認
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
                // 仕事があったら実行する（店長自ら調理場へ！）
                if (job.task)
                {
                    job.task();
                }
                // 終わったらカウンターを減らす
                if (job.counter)
                {
                    job.counter->count.fetch_sub(1);
                }
            }
            else
            {

                // 仕事もないなら、仕方ないのでYieldして他のスレッドにCPUを譲る
                // (まだカウンターが0じゃない＝誰かが実行中)
                std::this_thread::yield();
            }
        }
    }

    // =======================================================================
    // シェフの業務マニュアル（WorkerLoop）
    // =======================================================================
    void JobSystem::WorkerLoop(unsigned int workerIndex)
    {
        // スレッドに名前を付ける（Tracy のスレッド一覧に出る名前）
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

        while (true)
        {                           // 基本的に死ぬまで働く無限ループ
            InternalJob currentJob; // 自分の手元に持ってくる仕事入れ（お盆）

            // ---【エリアA：注文ボードの前（立ち入り禁止区域）】---
            {
                // 1. 鍵（Mutex）をかける。
                // これで、他のシェフや店長はこの { } の中に入れない。
                // 注文ボード(m_jobQueue)を触れるのは、世界で自分一人だけ！
                std::unique_lock<std::mutex> lock(m_queueMutex);

                // 2. 「仕事が来る」または「閉店(m_shutdown)」まで寝て待つ
                // wait は待機に入る際に lock を解放し、起床時に再取得してから戻る。
                // そのため待っている間、他スレッドは注文ボードを触れる。
                m_condition.wait(lock, [this] { return m_shutdown || !m_jobQueue.empty(); });

                // 起きた時に「閉店」かつ「仕事も空っぽ」なら、帰宅(return)する
                if (m_shutdown && m_jobQueue.empty())
                {
                    return; // スレッド終了
                }

                // 3. ボードから一番前の注文票を1枚剥がして、自分の手元(currentJob)に移す
                currentJob = std::move(m_jobQueue.front()); // コピー・移動
                m_jobQueue.pop();                           // ボードから削除（破り捨てる）
            }
            // ---【エリアA終了：ロック解除】---
            // ここで `lock` 変数が消滅するので、自動的に鍵が開く。
            // これで他のシェフもボードに近づけるようになる。

            // ---【エリアB：調理場（並列作業エリア）】---
            // ここはロックしていないので、全シェフが同時に作業できる！

            // 4. 仕事を実行！
            if (currentJob.task)
            {

                // 【バトンの開封と実行】
                // 先ほどカプセル化された [sys, dt]() { sys->UpdateImpl(dt); } がここで発動！
                currentJob.task(); // ここで重い処理（物理演算など）が走る
            }

            // 5. 完了報告
            // 「一丁あがり！」カウンターを1減らす。
            // 0になったら、待っている店長（WaitForCounter）が解放される。
            if (currentJob.counter)
            {
                currentJob.counter->count.fetch_sub(1);
            }
        }
    }

} // namespace CCL::ECS