/**
 * @file DroneMovementSystem.cpp
 * @brief ドローン移動システムの実装
 */
#include "DroneMovementSystem.h"
#include "Game/Core/SystemPriority.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void DroneMovementSystem::Update(float dt) {

    // テンプレート制約通り、TransformをWrite、DroneをReadで受け取る
    ForEachParallel([dt](TransformComponent& trans, const DroneComponent& drone) {

        switch (drone.currentState) {
        case DroneState::MoveToTarget: {
            // ========================================================
            // 滑らかな追従（Lerp）
            // ========================================================
            trans.position = Vector3::Lerp(trans.position, drone.targetPosition, 5.0f * dt);

            // 進行方向への滑らかな回転（Slerp）
            Vector3 dir = drone.targetPosition - trans.position;
            if (dir.LengthSquared() > 0.001f) {
                float targetYaw = atan2f(dir.x, dir.z);
                Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
                trans.rotation = Quaternion::Slerp(trans.rotation, targetRot, 10.0f * dt);
                trans.isDirty = true; // Transformの更新を通知
            }
            break;
        }
        case DroneState::FireCharge: {
            // ========================================================
            // 目標座標への直線的かつ高速な突撃
            // ========================================================
            Vector3 dir = drone.targetPosition - trans.position;
            if (dir.LengthSquared() > 1.0f) {
                dir.Normalize();
                trans.position = trans.position + dir * drone.moveSpeed * dt;
                trans.isDirty = true;
            }
            break;
        }
        case DroneState::LockOn:
            // 発射直前のタメ状態（静止）
            break;
        case DroneState::Idle:
        default:
            break;
        }
        });
}

// 実際の座標更新を行うため、L04_Physicsを指定
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L04_Physics);