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
    if (!_world->HasResource<ShapeRenderer *>()) return;
    auto *renderer = _world->GetResource<ShapeRenderer *>();
    if (!renderer) return;

    ForEach([&](const TransformComponent              &trans,
                const CharacterMovementInputComponent &input,
                const JoltCharacterHandleComponent    &handle) {
        if (!handle.character) return;

        XMFLOAT3 pos = trans.GetWorldPosition();
        pos.y += 1.0f; // キャラクターの頭上に矢印を描画

        // ==========================================================
        // ★ 描画用のスケール係数（実際の速度の20%の長さにする）
        // ==========================================================
        const float DRAW_SCALE = 0.2f;
        const float HEAD_SIZE  = 0.05f; // 矢印の頭のサイズも小さく

        // 1. 理想の速度（青色の矢印）
        XMFLOAT3 desiredVel = {
            input.currentVelocity.x * DRAW_SCALE,
            input.currentVelocity.y * DRAW_SCALE,
            input.currentVelocity.z * DRAW_SCALE};
        XMFLOAT3 desiredEnd = {pos.x + desiredVel.x, pos.y + desiredVel.y, pos.z + desiredVel.z};
        renderer->DrawArrow(pos, desiredEnd, HEAD_SIZE, {0.2f, 0.2f, 1.0f, 1.0f});

        // 2. 現実の速度（赤色の矢印）
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

// 描画系のフェーズに登録
REGISTER_RENDER_SYSTEM(JoltCharacterDebugDrawSystem, Priority::RenderStage::R08_Main);