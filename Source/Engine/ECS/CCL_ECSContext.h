#pragma once
// ===========================================================================
// 概要: 1つの ECS 環境（World と SystemManager の組）を持つクラス
//
// 責務:
//   - World と SystemManager を作って結び付ける
//   - 1フレームの手順（予約の確定、処理対象の一覧の作り直し、System の実行）を決める
//
// 備考: 一覧の作り直しは、System が1つも動いていない境目でだけ行う
//         ロジックの更新の頭 : 予約を確定し、一覧を作り直す（ApplyStructureChanges）
//         ロジックの途中     : Prefab::SpawnPrefab がその場で確定するが、一覧は作り直さない
//         描画の更新の頭     : 確定済みの変化があれば、一覧だけ作り直す
//       ロジックの途中で生まれた Entity は、同じステップの後ろの System からは見えず、同じフレームの描画から見える
// ===========================================================================
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemManager.h"
#include <memory>

namespace CCL::ECS
{

    class ECSContext
    {
    public:
        // jobSystem は EngineServices が持つ共有のプール。World ごとには持たず、SystemManager へ渡す
        // explicit ... 暗黙的な変換を禁止する
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

        // 積んだ構造の変更を当て、各 System の処理対象の Chunk を求め直す。通るまで、生成した Entity は ForEach に入らない
        // 毎ステップの UpdateLogic の頭と、シーンの開始（BaseScene::Start）で呼ぶ
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

            // ロジックの途中でプレハブを生成すると、その場で確定するが一覧は古いまま残る（ファイル冒頭の備考）
            // そのまま描画の System を走らせると、新しい Entity や Chunk を移った Entity が見えない
            // 確定済みの変化だけを一覧へ反映する（予約の確定はしない。確定はロジックの更新の頭で行う）
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

}
