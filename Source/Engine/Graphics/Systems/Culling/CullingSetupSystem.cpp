#include "CullingSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"



using namespace CCL::ECS;

// =======================================================================
// Modelのセットアップ
// =======================================================================

void ModelCullingSetupSystem::Update(float dt)
{
    std::vector<EntityID> needsBoundingBox;
    std::vector<EntityID> needsVisibility;

    // 1. 足りないコンポーネントを【個別に】調査する
    ForEachWithID([&](EntityID id, const ModelComponent& model) {
        if (!m_world->HasComponent<BoundingBoxComponent>(id)) {
            needsBoundingBox.push_back(id);
        }
        if (!m_world->HasComponent<VisibilityComponent>(id)) {
            needsVisibility.push_back(id);
        }
        });

    // 2. 足りないものだけを【個別に】付与する
    for (auto id : needsBoundingBox) {
        m_world->AddComponent<BoundingBoxComponent>(id);
    }
    for (auto id : needsVisibility) {
        m_world->AddComponent<VisibilityComponent>(id);
    }
}

// =======================================================================
// Primitiveのセットアップ
// =======================================================================

void PrimitiveCullingSetupSystem::Update(float dt)
{
    std::vector<EntityID> needsBoundingBox;
    std::vector<EntityID> needsVisibility;

    // 1. 足りないコンポーネントを【個別に】調査する
    ForEachWithID([&](EntityID id, const PrimitiveComponent& prim) {
        if (!m_world->HasComponent<BoundingBoxComponent>(id)) {
            needsBoundingBox.push_back(id);
        }
        if (!m_world->HasComponent<VisibilityComponent>(id)) {
            needsVisibility.push_back(id);
        }
        });

    // 2. 足りないものだけを【個別に】付与する
    for (auto id : needsBoundingBox) {
        m_world->AddComponent<BoundingBoxComponent>(id);
    }
    for (auto id : needsVisibility) {
        m_world->AddComponent<VisibilityComponent>(id);
    }
}

// 実行順序：カリング計算（AABB生成や交差判定）の前に登録する
REGISTER_RENDER_SYSTEM(ModelCullingSetupSystem, Priority::RenderStage::R05_Culling);
REGISTER_RENDER_SYSTEM(PrimitiveCullingSetupSystem, Priority::RenderStage::R05_Culling);