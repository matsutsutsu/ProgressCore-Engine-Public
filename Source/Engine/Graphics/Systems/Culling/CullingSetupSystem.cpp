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

    ForEachWithID([&](EntityID id, const ModelComponent &model) {
        if (!m_world->HasComponent<BoundingBoxComponent>(id))
        {
            needsBoundingBox.push_back(id);
        }
        if (!m_world->HasComponent<VisibilityComponent>(id))
        {
            needsVisibility.push_back(id);
        }
    });

    for (auto id : needsBoundingBox)
    {
        m_world->AddComponent<BoundingBoxComponent>(id);
    }
    for (auto id : needsVisibility)
    {
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

    ForEachWithID([&](EntityID id, const PrimitiveComponent &prim) {
        if (!m_world->HasComponent<BoundingBoxComponent>(id))
        {
            needsBoundingBox.push_back(id);
        }
        if (!m_world->HasComponent<VisibilityComponent>(id))
        {
            needsVisibility.push_back(id);
        }
    });

    for (auto id : needsBoundingBox)
    {
        m_world->AddComponent<BoundingBoxComponent>(id);
    }
    for (auto id : needsVisibility)
    {
        m_world->AddComponent<VisibilityComponent>(id);
    }
}



// 付けたコンポーネントは次のロジック更新の先頭で反映されるので、同じ R05 でも順番は問わない
REGISTER_RENDER_SYSTEM(ModelCullingSetupSystem, Priority::RenderStage::R05_Culling);
REGISTER_RENDER_SYSTEM(PrimitiveCullingSetupSystem, Priority::RenderStage::R05_Culling);