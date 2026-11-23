#include "PreviewGridDrawSystem.h"

#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace DirectX;

namespace
{
    const XMFLOAT4 kAxisXColor(0.9f, 0.25f, 0.25f, 1.0f); // 左右の軸
    const XMFLOAT4 kAxisZColor(0.3f, 0.5f, 1.0f, 1.0f);   // 前後の軸

    constexpr float kLift = 0.02f; // 床の面と重なって線がちらつかないよう、少し浮かせる [m]
}



void PreviewGridDrawSystem::Update(float dt)
{
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent &trans, const PreviewGridComponent &grid) {
        if (!grid.visible) return;
        DrawGrid(renderer, trans.GetWorldPosition(), grid);
    });
}



void PreviewGridDrawSystem::DrawGrid(ShapeRenderer *renderer, const XMFLOAT3 &center, const PreviewGridComponent &grid) const
{
    // 床の面と重なって線がちらつかないように少し浮かせる
    const float spacing = (grid.spacing > 0.0f) ? grid.spacing : 1.0f;
    const int   half    = (grid.halfCount > 0) ? grid.halfCount : 1;
    const int   major   = (grid.majorEvery > 0) ? grid.majorEvery : 1;
    const float extent  = spacing * half;
    const float y       = center.y + kLift;

    for (int i = -half; i <= half; ++i)
    {
        const float offset = spacing * i;

        const XMFLOAT4 &lineColor = (i % major == 0) ? grid.majorColor : grid.color;
        const XMFLOAT4 &alongX    = (i == 0) ? kAxisXColor : lineColor; // X に沿う線（Z が一定）
        const XMFLOAT4 &alongZ    = (i == 0) ? kAxisZColor : lineColor; // Z に沿う線（X が一定）

        renderer->DrawLine({center.x - extent, y, center.z + offset}, {center.x + extent, y, center.z + offset}, alongX);
        renderer->DrawLine({center.x + offset, y, center.z - extent}, {center.x + offset, y, center.z + extent}, alongZ);
    }
}



// R08_Main で ShapeRenderer に積む
REGISTER_DEBUG_RENDER_SYSTEM(PreviewGridDrawSystem, Priority::RenderStage::R08_Main);
