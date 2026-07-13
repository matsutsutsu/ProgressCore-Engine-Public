/// @file  DroneMovementSystem.cpp
/// @brief ドローン移動システムの実装（基礎3ステート版）。
///
/// FireCharge は内部で stateTimer を使い「タメ→リリース」の2フェーズを持つ
/// （旧 LockOn の役割を吸収）。これにより外部からは FireCharge 1つを
/// 見ているだけで「予備動作→突撃」という企画上重要な予兆表現ができる。
#include "DroneMovementSystem.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void DroneMovementSystem::Update(float rawDt)
{
    ForEachParallel([this](TransformComponent &trans, DroneComponent &drone,
                        const TimeStateComponent &time) {
        // ドローン各自の時計を使用（被弾でヒットストップ等がかかった時にピタッと止まる）
        float dt = time.localDt;

        // ====================================================================
        // 【リード（Leash）によるスタック救済】ボスから遠すぎたら強制ワープ
        // ====================================================================
        //if (const auto *bossTrans = _world->GetComponent<TransformComponent>(drone.ownerBossId))
        //{
        //    const float     distToBossSqr = (trans.position - bossTrans->position).LengthSquared();
        //    constexpr float leashRadius   = 140.0f;

        //    if (distToBossSqr > (leashRadius * leashRadius))
        //    {
        //        trans.position        = bossTrans->position + Vector3(0.0f, 15.0f, 0.0f);
        //        drone.currentVelocity = Vector3::Zero;
        //        drone.currentState    = DroneState::MoveToTarget;
        //        trans.isDirty         = true;
        //        trans.isTeleported    = true; // 瞬間移動なので Push 側に teleport と伝える
        //    }
        //}

        drone.hoverTimeOffset += dt;

        // --------------------------------------------------------------
        // 各ステートの「意思決定」：drone.currentVelocity / rotation を更新する
        // --------------------------------------------------------------
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            ProcessMoveToTarget(trans, drone, dt);
            break;
        case DroneState::Idle:
            ProcessIdle(trans, drone, dt);
            break;
        case DroneState::FireCharge:
            ProcessFireCharge(trans, drone, dt);
            break;
        }

        // 速度を積分して座標を確定する（Jolt へは Push が同期する）
        trans.position = trans.position + drone.currentVelocity * dt;

        // 回転はクォータニオンのドリフトを毎フレーム正規化してから確定
        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
        quatVec                   = DirectX::XMQuaternionNormalize(quatVec);
        DirectX::XMStoreFloat4(&trans.rotation, quatVec);

        trans.isDirty = true;
    });
}

// ============================================================================
// 各ステートの具体的な「意思決定」ロジック
// ============================================================================

inline void DroneMovementSystem::ProcessMoveToTarget(TransformComponent &trans, DroneComponent &drone, float dt)
{
    float pGain   = 20.0f;
    float damping = 15.0f;

    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = displacement * pGain;

    float maxSpeed = drone.moveSpeed * 2.5f;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, (std::min)(damping * dt, 1.0f));

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        Vector3 lookDir = drone.targetLookDirection;
        lookDir.Normalize();
        float      targetYaw   = atan2f(lookDir.x, -lookDir.z);
        float      targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
        Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
        trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, (std::min)(25.0f * dt, 1.0f));
    }
}

inline void DroneMovementSystem::ProcessIdle(TransformComponent &trans, DroneComponent &drone, float dt)
{
    Vector3 displacement   = drone.targetPosition - trans.position;
    float   pGain          = 10.0f;
    float   damping        = 8.0f;
    Vector3 targetVelocity = displacement * pGain;

    float maxSpeed = drone.moveSpeed * 1.5f;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, damping * dt);

    // ホバー感（フワフワ）
    float hoverFrequency = 1.0f;
    float hoverAmplitude = 3.0f;
    drone.currentVelocity.y += std::cos(drone.hoverTimeOffset * hoverFrequency) * hoverAmplitude * dt;

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 15.0f * dt);
    }
}

/// @brief 突撃。stateTimer > 0 の間は「タメ」（旧LockOnを吸収）、
///        0になった瞬間から「リリース」（高速直進）に切り替わる2フェーズ構造。
inline void DroneMovementSystem::ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt)
{
    if (drone.fireDirection.LengthSquared() <= 0.001f)
    {
        drone.currentVelocity = Vector3::Zero;
        return;
    }

    if (drone.stateTimer > 0.0f)
    {
        // ------------------------------------------------------------
        // [フェーズ1] タメ（予備動作）：後ろに引き絞りつつ震える
        //   ジャスト回避で狙う「読ませ」の時間帯。
        // ------------------------------------------------------------
        drone.stateTimer -= dt;

        float drawbackSpeed   = 2.0f;
        float upwardLiftSpeed = 4.0f;
        float jitterAmplitude = 5.5f;
        float jitterSpeed     = 100.0f;

        drone.currentVelocity = (drone.fireDirection * drawbackSpeed) + (Vector3::UnitY * upwardLiftSpeed);
        drone.currentVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
        drone.currentVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
        drone.currentVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
    }
    else
    {
        // ------------------------------------------------------------
        // [フェーズ2] リリース：高速直進。この間だけ「危険」＝ジャスト回避の対象。
        // ------------------------------------------------------------
        float chargeSpeedMultiplier = 2.5f;
        drone.currentVelocity       = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
    }
}

// L03_PrePhysics で確定させる（PushSystem より前に trans を仕上げる）
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);