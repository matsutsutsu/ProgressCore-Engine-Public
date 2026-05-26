/**
 * @file   CCL_SystemManager.h
 * @brief  Systemの登録・有効/無効切替・更新・対象Chunk再構築を行う管理クラス
 *
 * @details
 *  SystemManagerはWorldと連携し、登録された全Systemに対し以下を行います:
 *   - フィルタにマッチするChunkを毎フレーム自動更新 (ResolveTargetChunks)
 *   - TaskSchedulerによる並列実行
 *   - 構造変更後にChunkキャッシュを再構築
 *
 *  RegisterSystem<T> はテンプレですが、内部では SystemBase ポインタのみ扱うため
 *  本ヘッダ自体は CCL_World.h を引きずらず、CCL_SystemBase.h のみで成立します。
 *
 *  【依存】
 *   - CCL_SystemBase.h
 *   - CCL_TaskScheduler.h
 *   - \<vector\>, \<memory\>, \<unordered_map\>
 *
 * @author CCL ECS Library
 */
#pragma once

#include "CCL_SystemBase.h"
#include "CCL_TaskScheduler.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <utility>


namespace CCL::ECS
{
    namespace Core
    {
        class World;
        class JobSystem;
        class Chunk;
    }


    /**
     * @class SystemManager
     * @brief System群の登録・実行を取り仕切る
     */
    class SystemManager
    {
    public:
        SystemManager();
        ~SystemManager();

        SystemManager(const SystemManager&)            = delete;
        SystemManager& operator=(const SystemManager&) = delete;


        // =====================================================================
        //  接続
        // =====================================================================

        void SetWorld(Core::World* world) noexcept     { _world = world; }
        void SetJobSystem(Core::JobSystem* js) noexcept{ _jobSystem = js; }


        // =====================================================================
        //  System登録
        // =====================================================================

        /**
         * @brief System Tをインスタンス化して登録する
         * @tparam T    SystemBase派生クラス
         * @tparam Args Tのコンストラクタ引数
         * @return 登録されたSystemへの参照
         *
         * @details
         *  登録時に World/JobSystem/StructureMutex を自動でinjectし、
         *  Initialize()を1度だけ呼びます。
         */
        template <class T, typename... Args>
        T& RegisterSystem(Args&&... args)
        {
            static_assert(std::is_base_of_v<SystemBase, T>,
                "T must derive from SystemBase");

            auto sys = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw   = sys.get();

            InjectAndInitialize(raw);
            _systems.emplace_back(std::move(sys));

            _needsRebuild = true; // バッチ再構築フラグ
            return *raw;
        }


        /// @brief 登録済みSystemのリスト
        const std::vector<std::unique_ptr<SystemBase>>& GetSystems() const noexcept
        {
            return _systems;
        }


        // =====================================================================
        //  実行
        // =====================================================================

        /**
         * @brief 全Systemを1フレーム分実行
         * @param dt 経過時間(秒)
         * @details
         *  - World->IsStructureDirty() なら対象Chunkを再構築
         *  - TaskScheduler::BuildGraph (初回 or 必要時のみ)
         *  - TaskScheduler::Execute で実行
         */
        void Update(float dt);


        /**
         * @brief デバッグGUI描画 (派生System側のOnGuiを呼ぶ)
         * @details ImGui等を利用する場合は、外側で `ImGui::Begin/End` を作ってから呼んでください。
         */
        void OnGui();


        // =====================================================================
        //  クエリ
        // =====================================================================

        /**
         * @brief 名前でSystemを検索 (デバッグ/GUI用)
         * @param name 完全一致するSystem名
         * @return 見つかればポインタ、なければ nullptr
         */
        SystemBase* FindSystemByName(const std::string& name) const;


        /// @brief TaskSchedulerのバッチ列を取得 (デバッグ表示用)
        const std::vector<std::vector<SystemBase*>>& GetBatches() const
        {
            return _scheduler.GetBatches();
        }


    private:
        // =====================================================================
        //  内部ヘルパ
        // =====================================================================

        /// @brief 登録Systemへ World/JobSystem/Mutex を注入し Initialize() を呼ぶ
        void InjectAndInitialize(SystemBase* sys);

        /**
         * @brief 各SystemのFilterに合致するChunkを再収集
         * @details ApplyPendingOperationsの直後に呼ばれる想定です。
         *          フィルタが空のSystemは全Chunkを対象とします。
         */
        void ResolveTargetChunks();


    private:
        std::vector<std::unique_ptr<SystemBase>> _systems;
        TaskScheduler                             _scheduler;

        Core::World*       _world      = nullptr;
        Core::JobSystem*   _jobSystem  = nullptr;

        bool               _needsRebuild = true; ///< 次回Update時にBuildGraphを呼ぶか
    };

} // namespace CCL::ECS
