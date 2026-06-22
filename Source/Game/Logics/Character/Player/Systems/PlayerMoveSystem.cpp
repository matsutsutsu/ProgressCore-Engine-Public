#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID id,
        TransformComponent& trans,
        const CharacterMovementInputComponent& inputData,
        const JoltCharacterConfigComponent& config,
        const JoltCharacterHandleComponent& joltHandle,
        const PlayerComponent& player,
        const TimeStateComponent& time)
        {
            if (_world->HasComponent<Tag::DeadTag>(id)) return;
            if (!joltHandle.character) return; // Jolt実体が無い場合はスキップ

            // =========================================================
            // モデルの振り向き (TPS仕様) のみ担当する
            // =========================================================
            Vector3 horizontalMove(inputData.desiredVelocity.x, 0.0f, inputData.desiredVelocity.z);

            if (horizontalMove.LengthSquared() > 0.001f) {
                horizontalMove.Normalize();

                float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);
                Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);

                // 描画用のTransformを回転させる（TimeScale適用済みDtを使用）
                trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, player.turnSpeed * time.localDt);
                DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation));
                trans.isStatic = false;

                // 物理カプセルの向きも同期させる (直立カプセルの向き)
                joltHandle.character->SetRotation(JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w));
            }
        });
}

REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_Update);