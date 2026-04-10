#include "CullingSetupSystem.h"
#include "ECS/Core/CCL_World.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"



using namespace CCL::ECS;

// =======================================================================
// Modelのセットアップ
// =======================================================================
ModelCullingSetupSystem::ModelCullingSetupSystem() : IfSystem("ModelCullingSetupSystem") {}

void ModelCullingSetupSystem::Update(float dt)
{
    // イテレータの破壊を防ぐため、追加対象のIDリストを作る
    std::vector<EntityID> targetEntities;

    // 1. ループで追加対象を検索
    ForEachWithID([&](EntityID id, const ModelComponent& model) {
        if (!_world->HasComponent<BoundingBoxComponent>(id)) {
            targetEntities.push_back(id);
        }
        });

    // 2. ループ外で安全にコンポーネントを付与（構造的変更）
    for (auto id : targetEntities) {
        _world->AddComponent<BoundingBoxComponent>(id);
        _world->AddComponent<VisibilityComponent>(id);
    }
}

// =======================================================================
// Primitiveのセットアップ
// =======================================================================
PrimitiveCullingSetupSystem::PrimitiveCullingSetupSystem() : IfSystem("PrimitiveCullingSetupSystem") {}

void PrimitiveCullingSetupSystem::Update(float dt)
{
    // イテレータの破壊を防ぐため、追加対象のIDリストを作る
    std::vector<EntityID> targetEntities;

    // 1. ループで追加対象を検索
    ForEachWithID([&](EntityID id, const PrimitiveComponent& prim) {
        if (!_world->HasComponent<BoundingBoxComponent>(id)) {
            targetEntities.push_back(id);
        }
        });

    // 2. ループ外で安全にコンポーネントを付与（構造的変更）
    for (auto id : targetEntities) {
        _world->AddComponent<BoundingBoxComponent>(id);
        _world->AddComponent<VisibilityComponent>(id);
    }
}

// 実行順序：カリング計算（AABB生成や交差判定）の前に登録する
// ※必要に応じてあなたのSystemPriorityの設定に合わせて変更してください
REGISTER_RENDER_SYSTEM(ModelCullingSetupSystem, Priority::RenderStage::R05_Culling);
REGISTER_RENDER_SYSTEM(PrimitiveCullingSetupSystem, Priority::RenderStage::R05_Culling);