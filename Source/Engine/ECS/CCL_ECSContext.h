#pragma once
// ===========================================================================
// 概要: 1つの独立した ECS 環境（World と SystemManager の組）を持つクラス。
//
// 責務:
//   - World と SystemManager の生成と結び付け
//   - 1フレームの更新手順（構造変更の適用 → 処理対象の再解決 → System 実行）を固定する
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include <memory>

namespace CCL::ECS
{

    class ECSContext
    {
    public:
        /**
         * @param jobSystem 上層（EngineServices）が持つ共有ジョブプール。
         *                  World ごとには持たず、SystemManager へ注入して使う
         *
         * @note explicit は、`ECSContext ctx = jobSystem;` のような暗黙変換での
         *       構築を禁止する指定。付けなくても動くが、意図しない変換を防げる。
         */
        explicit ECSContext(JobSystem *jobSystem = nullptr)
        {
            m_world         = std::make_unique<World>();
            m_systemManager = std::make_unique<SystemManager>();

            // 登録する System へ配る World を SystemManager に渡す
            m_systemManager->SetWorld(m_world.get());

            // 共有ジョブプールを注入（RegisterSystems より前に済ませる）
            m_systemManager->SetJobSystem(jobSystem);
        }

        ~ECSContext() = default;

        // ============================================================
        // 更新（呼ぶ順番をここに固定し、利用側で手順を間違えないようにする）
        // ============================================================

        // 物理・ロジックの更新（FixedUpdate 等で呼ぶ）
        void UpdateLogic(float dt)
        {
            ZoneScopedN("World::FixedUpdate");

            // 1. 溜まっていた構造変更（Entity 生成/削除）を適用し、データを確定させる
            m_world->ScrutinyAndApply();

            // 2. 構造変更があったフレームだけ、各 System の処理対象 Chunk を再解決する
            if (m_world->IsStructureDirty())
            {
                m_systemManager->ResolveTargetChunks(&m_world->GetChunkManager());
                m_world->ResetStructureDirty();
            }

            // 3. Logic グループの System を実行する
            m_systemManager->UpdateLogic(dt);
        }

        // 描画系の更新（RenderUpdate 等で呼ぶ）
        void UpdateRender(float dt)
        {
            ZoneScopedN("World::RenderUpdate");

            m_systemManager->UpdateRender(dt);
        }

        // ポーズ画面用など、ロジックは止めて構造変更と描画だけを行う特殊更新
        void UpdatePaused(float dt)
        {
            m_world->ScrutinyAndApply();
            m_systemManager->ResolveTargetChunks(&m_world->GetChunkManager());
            m_systemManager->UpdateRender(dt);
        }

        // ============================================================
        // アクセッサ
        // ============================================================

        World         *GetWorld() const { return m_world.get(); }
        SystemManager *GetSystemManager() const { return m_systemManager.get(); }

    private:
        std::unique_ptr<World>         m_world;
        std::unique_ptr<SystemManager> m_systemManager;
    };

} // namespace CCL::ECS
