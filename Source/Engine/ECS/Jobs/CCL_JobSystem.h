#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// ===================================================================================
// ファイル名: CCL_JobSystem.h
// クラス名: JobSystem
//
// 【概要】
//   マルチスレッド処理を行うための「スレッドプール」管理クラスです。
//   CPUのコア数分だけ事前にスレッド（作業員）を作成し、
//   投げられたタスク（ジョブ）を並列で消化させます。
//
// 【イメージ：レストランの厨房】
//   - JobSystem     : 厨房そのもの
//   - Worker        : 雇われたシェフたち（スレッド）
//   - JobQueue      : 注文票が貼られるボード（仕事リスト）
//   - Mutex         : ボードの前の「立ち入り禁止テープ」（排他制御の鍵）
//   - ConditionVar  : シェフを起こす「呼び出しベル」
//   - JobCounter    : 料理が全部出たか確認する「伝票の残り枚数」
// ===================================================================================


namespace CCL::ECS {

    // Job = 「引数なし・戻り値なし」の関数（ラムダ式など）
    // 例：「野菜を切る」「皿を洗う」「Chunk0番を更新する」といった具体的な作業内容
    using Job = std::function<void()>;

    // -----------------------------------------------------------------------
    // JobCounter: 仕事の完了待ちをするためのカウンター
    // -----------------------------------------------------------------------
    // 素の int では複数スレッドからの増減が競合して値が壊れるため、
    // 不可分に増減できる std::atomic を使う。
    struct JobCounter {
        std::atomic<int> count{0};
    };

    class JobSystem {
      public:
        // コンストラクタ：厨房を開き、シェフを雇う
        JobSystem();

        // デストラクタ：店じまいし、シェフを帰宅させる
        ~JobSystem();

        // コピー禁止（厨房は世界に一つだけ。シングルトンや管理クラスで持つため）
        JobSystem(const JobSystem &)            = delete;
        JobSystem &operator=(const JobSystem &) = delete;

        /**
         * @brief 仕事をキューに追加する（店長が注文を入れる）
         * @param job 実行したい処理（ラムダ式など）
         * @param counter 完了を追跡するためのカウンター（nullptrなら追跡しない）
         */
        void Execute(Job job, JobCounter *counter = nullptr);

        /**
         * @brief 複数の仕事を「1回のロックで」まとめてキューに追加する。
         *
         * Execute() を N 回呼ぶと mutex の獲得・解放が N 回発生する。
         * 1回の投入が数百〜数千件になる用途（Jolt の物理ジョブなど）では
         * このロック競合そのものが並列化の利得を食い潰すため、
         * まとめて1回のロックで積むための入口を用意している。
         *
         * @param inJobs  実行したい処理の配列（中身はムーブされる）
         * @param inCount 件数
         * @param counter 完了を追跡するカウンター（nullptrなら追跡しない）
         */
        void ExecuteBatch(Job *inJobs, size_t inCount, JobCounter *counter = nullptr);

        /**
         * @brief 指定したカウンターが0になるまで、メインスレッドを待機させる
         * （料理が全部出るまで、店長は足止めを食らう）
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
        // シェフ（ワーカースレッド）が実行する業務マニュアル
        // 生まれた瞬間から死ぬまで、このループの中で働き続けます。
        //
        // workerIndex は Tracy 上のスレッド名（"ECS Worker 0" など）に使う。
        // 全員を同じ名前にすると、プロファイラ上でどのスレッドがどれか判別できず、
        // 物理(Jolt Worker N)との棲み分けも見えなくなるため通し番号を振る。
        void WorkerLoop(unsigned int workerIndex);

      private:
        // 内部で扱うジョブ構造体
        struct InternalJob {
            Job         task;    // やるべき仕事
            JobCounter *counter; // 終わったら減らすカウンター
        };

        std::vector<std::thread> m_workers;  // 働くシェフたち
        std::queue<InternalJob>  m_jobQueue; // 仕事の注文票ボード

        std::mutex m_queueMutex; // ボードを守る「鍵」（排他制御）
                                // これがないと複数のシェフが同時にチケットを取ろうとして破れます。

        std::condition_variable m_condition; // 寝ているシェフを起こす「ベル」

        bool m_shutdown = false; // 「閉店」フラグ
    };

} // namespace CCL::ECS