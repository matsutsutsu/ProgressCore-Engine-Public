#include "AABBDebugDrawSystem.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

AABBDebugDrawSystem::AABBDebugDrawSystem() : IfSystem("AABBDebugDrawSystem") {}

void AABBDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;

    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const BoundingBoxComponent &bounds, const VisibilityComponent &vis) {
        // 見えているなら緑、カリングされたなら赤
        DirectX::XMFLOAT4 color = vis.isVisible ? DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f)
                                                : DirectX::XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);

        DirectX::XMFLOAT3 zeroEuler = {0.0f, 0.0f, 0.0f};

        // AABB なので回転は 0
        renderer->DrawBox(
            bounds.worldAABB.Center,
            zeroEuler,
            bounds.worldAABB.Extents,
            color);
    });
}

REGISTER_RENDER_SYSTEM(AABBDebugDrawSystem, Priority::RenderStage::R08_Main);