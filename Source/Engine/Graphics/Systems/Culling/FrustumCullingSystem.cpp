#include "FrustumCullingSystem.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void FrustumCullingSystem::Update(float dt)
{
    if (!m_world->HasResource<Camera *>()) return;
    auto *camera = m_world->GetResource<Camera *>();
    if (!camera) return;

    // カメラが今フレーム計算した視錐台
    const DirectX::BoundingFrustum &frustum = camera->GetFrustum();

    ForEachParallel([&](const BoundingBoxComponent &bounds, VisibilityComponent &vis) {
        vis.isVisible = frustum.Intersects(bounds.worldAABB);
    });
}

REGISTER_RENDER_SYSTEM(FrustumCullingSystem, Priority::RenderStage::R05_Culling);