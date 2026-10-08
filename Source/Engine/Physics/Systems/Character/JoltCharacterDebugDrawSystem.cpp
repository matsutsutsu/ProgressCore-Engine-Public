#include "JoltCharacterDebugDrawSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace DirectX;
using namespace CCL::ECS;

void JoltCharacterDebugDrawSystem::Update(float dt)
{
    if (!isDebugVisible) return;
    if (!m_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = m_world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent           &trans,
                const CharacterMovementComponent   &movement,
                const JoltCharacterHandleComponent &handle) {
        if (!handle.character) return;

        XMFLOAT3 pos = trans.GetWorldPosition();
        pos.y += 1.0f; // 足元から 1m 上に描く

        const float DRAW_SCALE = 0.2f;  // 矢印の長さ = 速度(m/秒) × 0.2
        const float HEAD_SIZE  = 0.05f; // 矢印の頭の大きさ

        // 1. ロジックが求めた水平速度（青）
        XMFLOAT3 desiredVel = {
            movement.currentVelocity.x * DRAW_SCALE,
            movement.currentVelocity.y * DRAW_SCALE,
            movement.currentVelocity.z * DRAW_SCALE};
        XMFLOAT3 desiredEnd = {pos.x + desiredVel.x, pos.y + desiredVel.y, pos.z + desiredVel.z};
        renderer->DrawArrow(pos, desiredEnd, HEAD_SIZE, {0.2f, 0.2f, 1.0f, 1.0f});

        // 2. CharacterVirtual の実際の速度（赤）。見分けやすいよう少し上にずらす
        JPH::Vec3 actualJoltVel = handle.character->GetLinearVelocity();
        XMFLOAT3  actualVel     = {
            actualJoltVel.GetX() * DRAW_SCALE,
            actualJoltVel.GetY() * DRAW_SCALE,
            actualJoltVel.GetZ() * DRAW_SCALE};

        XMFLOAT3 actualStart = {pos.x, pos.y + 0.1f, pos.z};
        XMFLOAT3 actualEnd   = {actualStart.x + actualVel.x, actualStart.y + actualVel.y, actualStart.z + actualVel.z};
        renderer->DrawArrow(actualStart, actualEnd, HEAD_SIZE, {1.0f, 0.2f, 0.2f, 1.0f});
    });
}

REGISTER_RENDER_SYSTEM(JoltCharacterDebugDrawSystem, Priority::RenderStage::R08_Main);