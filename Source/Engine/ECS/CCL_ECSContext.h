#pragma once
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include <memory>

namespace CCL::ECS
{

    // 1つの独立したECS環境（データとロジックのセット）を管理するクラス
    class ECSContext
    {
    public:
        // jobSystem: 上層(EngineServices)が持つ共有ジョブプール。
        //   フレーム並列に使うためSystemManagerへ注入する（World毎に持たない）。
        // explicit... コンストラクタを「ECSContext ctx = ...」のように暗黙的に呼ばせないための指定
        // 別に書かなくてもいいけど、書いておくと「ECSContext ctx = ...」のような書き方を禁止できる
        explicit ECSContext(JobSystem *jobSystem = nullptr)
        {
            m_world         = std::make_unique<World>();
            m_systemManager = std::make_unique<SystemManager>();

            // SystemManagerに「お前が処理すべきデータはこれだ」と教える
            m_systemManager->SetWorld(m_world.get());

            // 共有ジョブプールを注入（RegisterSystems より前に済ませる）
            m_systemManager->SetJobSystem(jobSystem);
        }

        ~ECSContext() = default;

        // ---------------------------------------------------------
        // チームメンバー（利用者）が呼び出す安全な更新フロー
        // ---------------------------------------------------------

        // 物理・ロジックの更新（FixedUpdate等で呼ぶ）
        void UpdateLogic(float dt)
        {

            //  FixedUpdate全体を計測
            ZoneScopedN("World::FixedUpdate");

            // 1. 溜まっていた構造変更（Entity生成/削除）を適用し、データを確定させる
            m_world->ScrutinyAndApply();

            // 2. 構造変更があったフレームのみターゲットを再解決する
            if (m_world->IsStructureDirty())
            {
                m_systemManager->ResolveTargetChunks(&m_world->GetChunkManager());
                m_world->ResetStructureDirty(); // フラグを下ろす
            }

            // 3. ロジックを回す
            m_systemManager->UpdateLogic(dt);
        }

        // 描画系の更新（RenderUpdate等で呼ぶ）
        void UpdateRender(float dt)
        {

            // RenderUpdate全体を計測
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

        // ---------------------------------------------------------
        // アクセッサ
        // ---------------------------------------------------------
        World         *GetWorld() const { return m_world.get(); }
        SystemManager *GetSystemManager() const { return m_systemManager.get(); }

    private:
        std::unique_ptr<World>         m_world;
        std::unique_ptr<SystemManager> m_systemManager;
    };

} // namespace CCL::ECS
