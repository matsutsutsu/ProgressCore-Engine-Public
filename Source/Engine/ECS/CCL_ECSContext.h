#pragma once
// ===========================================================================
// 概要: 1つの独立した ECS 環境（World と SystemManager の組）を持つクラス。
//
// 責務:
//   - World と SystemManager の生成と結び付け
//   - 1フレームの更新手順（構造変更の確定 → 処理対象の一覧の作り直し → System 実行）を固定する
//
// 備考:
//   予約の確定と、System の処理対象 Chunk の一覧の作り直しを、それぞれ次のタイミングで行う。
//     ロジックの更新の頭 : 予約を確定し、一覧を作り直す（ApplyStructureChanges）
//     ロジックの途中     : Prefab::SpawnPrefab がその場で確定する。一覧は作り直さない
//                          （同じバッチで並列に走る System が、その一覧を回っている最中かもしれない）
//     描画の更新の頭     : 確定済みの変化があれば、一覧だけ作り直す（確定はしない）
//     シーンの開始       : ApplyStructureChanges を1回呼ぶ（BaseScene::Start）
//   一覧の作り直しは、System が1つも動いていない境目でだけ行う、が決まり。
//   ロジックの途中で生成した Entity は、同じステップの後ろの System には見えず、同じフレームの描画から見える。
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

        /**
         * @brief 溜まっていた構造変更（Entity 生成/削除）を適用し、各 System の処理対象 Chunk を解決し直す。
         *
         * これを通るまで、生成した Entity は System の ForEach に入らない。
         * 毎ステップの UpdateLogic の頭と、シーンの開始（BaseScene::Start）で呼ぶ。
         */
        void ApplyStructureChanges()
        {
            // 1. 溜まっていた構造変更（Entity 生成/削除）を適用し、データを確定させる
            m_world->ScrutinyAndApply();

            // 2. 構造変更があったときだけ、各 System の処理対象 Chunk を再解決する
            if (m_world->IsStructureDirty())
            {
                m_systemManager->ResolveTargetChunks(&m_world->GetChunkManager());
                m_world->ResetStructureDirty();
            }
        }

        // 物理・ロジックの更新（FixedUpdate 等で呼ぶ）
        void UpdateLogic(float dt)
        {
            ZoneScopedN("World::FixedUpdate");

            // 1. 構造変更を適用し、処理対象を解決し直す
            ApplyStructureChanges();

            // 2. Logic グループの System を実行する
            m_systemManager->UpdateLogic(dt);
        }

        // 描画系の更新（RenderUpdate 等で呼ぶ）
        void UpdateRender(float dt)
        {
            ZoneScopedN("World::RenderUpdate");

            // ロジックの途中でプレハブを生成すると、その場で確定するが一覧は古いまま残る（ファイル冒頭の備考）。
            // そのまま描画の System を走らせると、新しい Entity や Chunk を移った Entity が見えない。
            // 確定済みの変化だけを一覧へ反映する（予約の確定はしない。確定はロジックの更新の頭で行う）。
            if (m_world->IsStructureDirty())
            {
                m_systemManager->ResolveTargetChunks(&m_world->GetChunkManager());
                m_world->ResetStructureDirty();
            }

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
