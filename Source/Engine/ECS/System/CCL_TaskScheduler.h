/**
 * @file   CCL_TaskScheduler.h
 * @brief  Systemの依存解析を行い、並列実行可能なバッチ列を構築するスケジューラ
 *
 * @details
 *  各SystemはGetReadTypes / GetWriteTypesを通じて「触れる型」を申告します。
 *  TaskSchedulerはこの情報を元に「同時に走らせるとデータ競合を起こす」System同士
 *  を直列化し、それ以外は並列バッチに詰めます。
 *
 *  @par バッチの構築ルール
 *   - 2つのSystemが衝突: 一方が書き、もう一方が読みまたは書き
 *   - 衝突するSystemは別バッチへ配置
 *   - 同一バッチ内のSystemは _jobSystem 経由で並列実行可能
 *
 *  @par 本ヘッダの軽量化
 *   旧版はBuildGraphとExecuteの全実装がヘッダにありました。これらは非テンプレ
 *   なため本実装では .cpp に移し、ヘッダは宣言のみに抑えています。
 *
 *  【依存】
 *   - CCL_SystemBase.h (ポインタ操作のみ)
 *   - \<vector\>, \<memory\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_SystemBase.h"

#include <memory>
#include <vector>


namespace CCL::ECS
{
    namespace Core { class JobSystem; }


    /**
     * @class TaskScheduler
     * @brief System群の依存関係を解析し、並列実行可能なバッチ列を構築・実行する
     */
    class TaskScheduler
    {
    public:
        TaskScheduler()  = default;
        ~TaskScheduler() = default;

        TaskScheduler(const TaskScheduler&)            = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;


        // =====================================================================
        //  バッチ構築
        // =====================================================================

        /**
         * @brief System群を解析し、内部にバッチ列を構築
         * @param systems 対象System(unique_ptrで管理されていることを想定)
         */
        void BuildGraph(const std::vector<std::unique_ptr<SystemBase>>& systems);


        // =====================================================================
        //  実行
        // =====================================================================

        /**
         * @brief 構築済みバッチを順に実行 (バッチ内は並列, バッチ間は直列)
         * @param dt         経過時間
         * @param jobSystem  並列実行に使うJobSystem (nullptrならシングル実行)
         */
        void Execute(float dt, Core::JobSystem* jobSystem);


        // =====================================================================
        //  問い合わせ
        // =====================================================================

        /// @brief バッチ列を取得 (デバッグ表示用)
        const std::vector<std::vector<SystemBase*>>& GetBatches() const noexcept
        {
            return _executionBatches;
        }


    private:
        /**
         * @brief 2つのSystemが衝突するかを判定
         * @param a System A
         * @param b System B
         * @return Read/Write が競合していれば true
         */
        static bool IsConflict(const SystemBase* a, const SystemBase* b);

        /// @brief 型IDリスト内に指定値が含まれるか
        static bool Contains(const std::vector<TypeID>& list, TypeID id) noexcept;


    private:
        /// @brief 構築済みのバッチ列。インデックスが小さいほど先に実行
        std::vector<std::vector<SystemBase*>> _executionBatches;
    };

} // namespace CCL::ECS
