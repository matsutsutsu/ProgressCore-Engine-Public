/// @file  DroneMovementSystem.cpp
/// @brief ドローン移動システムの実装（Facade廃止・Transform直接積分版）。
///
/// 旧との違いはただ1点：
///   旧: 各 Process* が drone.currentVelocity を計算 → physicsAPI->SetLinearVelocity()
///   新: 各 Process* が drone.currentVelocity を計算 → Update側が trans.position += v*dt で積分
/// 各 Process* 内の演出ロジック（Lerp/Slerpのゲイン・震え等）は完全に流用。
#include "DroneMovementSystem.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void DroneMovementSystem::Update(float rawDt)
{
    m_currentTime += rawDt;

    ForEachWithIDParallel([this](EntityID id, TransformComponent &trans, DroneComponent &drone,
                              const TimeStateComponent &time) {
        // ドローン各自の時計を使用（被弾でヒットストップ等がかかった時にピタッと止まる）
        float dt = time.localDt;

        // ====================================================================
        // 【数学的ロールバック】前フレームで足した「揺れ」を一度引き算し、
        // 本来のクリーンな陣形座標に戻してから再計算する（旧と同じ）
        // ====================================================================
        trans.position           = trans.position - drone.currentShakeOffset;
        drone.currentShakeOffset = Vector3::Zero;

        // ====================================================================
        // 【リード（Leash）によるスタック救済】ボスから遠すぎたら強制ワープ
        // ====================================================================
        if (const auto *bossTrans = _world->GetComponent<TransformComponent>(drone.ownerBossId))
        {
            const float     distToBossSqr = (trans.position - bossTrans->position).LengthSquared();
            constexpr float leashRadius   = 140.0f;

            if (distToBossSqr > (leashRadius * leashRadius))
            {
                trans.position        = bossTrans->position + Vector3(0.0f, 15.0f, 0.0f);
                drone.currentVelocity = Vector3::Zero;
                drone.currentState    = DroneState::MoveToTarget;
                trans.isDirty         = true;
                trans.isTeleported    = true; // 瞬間移動なので Push 側に teleport と伝える
                // ★Facade呼び出しは無し。trans を書けば Push が Jolt へ反映する。
            }
        }

        drone.hoverTimeOffset += dt;

        // --------------------------------------------------------------
        // 各ステートの「意思決定」：drone.currentVelocity / rotation を更新する
        // Shaking だけは自前で position を直接書く特殊系（後述）
        // --------------------------------------------------------------
        switch (drone.currentState)
        {
        case DroneState::MoveToTarget:
            ProcessMoveToTarget(trans, drone, dt);
            break;
        case DroneState::FireCharge:
            ProcessFireCharge(trans, drone, dt);
            break;
        case DroneState::Idle:
            ProcessIdle(trans, drone, dt);
            break;
        case DroneState::LockOn:
            ProcessLockOn(trans, drone, dt);
            break;
        case DroneState::Hold:
            ProcessHold(trans, drone, dt);
            break;
        case DroneState::HorizontalCharge:
            ProcessHorizontalCharge(trans, drone, dt);
            break;
        case DroneState::Shaking:
            ProcessShaking(trans, drone, dt, m_currentTime);
            break;
        }

        // ====================================================================
        // ★ここが唯一の本質的な変更点★
        // 旧: physicsAPI->SetLinearVelocity(id, finalVel) で Jolt に積分を委譲していた
        // 新: ここで自分（ECS/ゲームプレイ層）が position を積分する。
        //     Shaking は Process 内で既に position を直接動かしている（ProcessMoveToTarget
        //     を内部で呼んでから震えを加算する構造）ので、二重積分しないよう除外する。
        // ====================================================================
        if (drone.currentState != DroneState::Shaking)
        {
            // タイムスケール（ヒットストップ中は遅くなる）を考慮して積分
            trans.position = trans.position + drone.currentVelocity * dt;
        }

        // 回転はクォータニオンのドリフトを毎フレーム正規化してから確定
        DirectX::XMVECTOR quatVec = DirectX::XMLoadFloat4(&trans.rotation);
        quatVec                   = DirectX::XMQuaternionNormalize(quatVec);
        DirectX::XMStoreFloat4(&trans.rotation, quatVec);

        // position/rotation を書いたので Jolt への反映を要求する
        trans.isDirty = true;

        (void)id; // Facade を使わなくなったので id はもう物理呼び出しに使わない
    });
}

// ============================================================================
// 各ステートの具体的な「意思決定」ロジック（演出は完全に旧から流用）
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

    // 回転（3D対応・-Z前方）
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

inline void DroneMovementSystem::ProcessFireCharge(TransformComponent &trans, DroneComponent &drone, float dt)
{
    if (drone.fireDirection.LengthSquared() <= 0.001f)
    {
        drone.currentVelocity = Vector3::Zero;
        return;
    }

    if (drone.stateTimer > 0.0f)
    {
        // [フェーズ1] アンティシペーション（タメ）
        drone.stateTimer -= dt;

        float drawbackSpeed   = 2.0f;
        float upwardLiftSpeed = 4.0f;
        float jitterAmplitude = 5.5f;
        float jitterSpeed     = 100.0f;

        drone.currentVelocity = (-drone.fireDirection * drawbackSpeed) + (Vector3::UnitY * upwardLiftSpeed);
        drone.currentVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
        drone.currentVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
        drone.currentVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
    }
    else
    {
        // [フェーズ2] 超高速突撃（リリース）
        float chargeSpeedMultiplier = 2.5f;
        drone.currentVelocity       = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
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

inline void DroneMovementSystem::ProcessLockOn(TransformComponent &trans, DroneComponent &drone, float dt)
{
    float pGain   = 15.0f;
    float damping = 10.0f;

    Vector3 displacement   = drone.targetPosition - trans.position;
    Vector3 targetVelocity = displacement * pGain;

    float maxSpeed = drone.moveSpeed * 1.5f;
    if (targetVelocity.LengthSquared() > maxSpeed * maxSpeed)
    {
        targetVelocity.Normalize();
        targetVelocity *= maxSpeed;
    }

    // 発射直前の激しい微振動
    targetVelocity.x += std::cos(drone.hoverTimeOffset * 50.0f) * 2.0f;
    targetVelocity.y += std::sin(drone.hoverTimeOffset * 60.0f) * 2.0f;
    targetVelocity.z += std::cos(drone.hoverTimeOffset * 40.0f) * 2.0f;

    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, targetVelocity, damping * dt);

    if (drone.targetLookDirection.LengthSquared() > 0.001f)
    {
        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 10.0f * dt);
    }
}

inline void DroneMovementSystem::ProcessHold(TransformComponent &trans, DroneComponent &drone, float dt)
{
    float damping         = 3.0f;
    drone.currentVelocity = Vector3::Lerp(drone.currentVelocity, Vector3::Zero, damping * dt);

    if (drone.currentVelocity.LengthSquared() < 0.01f)
    {
        drone.currentVelocity = Vector3::Zero;
    }
    // 回転は維持（何もしない）
    (void)trans;
}

inline void DroneMovementSystem::ProcessHorizontalCharge(TransformComponent &trans, DroneComponent &drone, float dt)
{
    if (drone.fireDirection.LengthSquared() <= 0.001f)
    {
        drone.currentVelocity = Vector3::Zero;
        return;
    }

    if (drone.stateTimer > 0.0f)
    {
        // [フェーズ1] タメ＋震え
        drone.stateTimer -= dt;

        float drawbackSpeed   = 4.0f;
        float jitterSpeed     = 100.0f;
        float jitterAmplitude = 4.5f;

        Vector3 baseVelocity = -drone.fireDirection * drawbackSpeed;
        baseVelocity.x += std::cos(drone.hoverTimeOffset * jitterSpeed) * jitterAmplitude;
        baseVelocity.y += std::sin(drone.hoverTimeOffset * (jitterSpeed * 1.2f)) * jitterAmplitude;
        baseVelocity.z += std::cos(drone.hoverTimeOffset * (jitterSpeed * 0.8f)) * jitterAmplitude;
        drone.currentVelocity = baseVelocity;

        if (drone.targetLookDirection.LengthSquared() > 0.001f)
        {
            Vector3 lookDir = drone.targetLookDirection;
            lookDir.Normalize();
            float      targetYaw   = atan2f(lookDir.x, -lookDir.z);
            float      targetPitch = asinf(std::clamp(lookDir.y, -1.0f, 1.0f));
            Quaternion targetRot   = Quaternion::CreateFromYawPitchRoll(targetYaw, targetPitch, 0.0f);
            trans.rotation         = Quaternion::Slerp(trans.rotation, targetRot, 20.0f * dt);
        }
    }
    else
    {
        // [フェーズ2] リリース
        float chargeSpeedMultiplier = 2.0f;
        drone.currentVelocity       = drone.fireDirection * (drone.moveSpeed * chargeSpeedMultiplier);

        float      targetYaw = atan2f(-drone.targetLookDirection.x, -drone.targetLookDirection.z);
        Quaternion targetRot = Quaternion::CreateFromAxisAngle(Vector3::UnitY, targetYaw);
        trans.rotation       = Quaternion::Slerp(trans.rotation, targetRot, 25.0f * dt);
    }
}

// ============================================================================
// 震え (Shaking) : 陣形移動を実行した上で、視覚的ノイズを position に直接加算する特殊系。
//   ProcessMoveToTarget が velocity を計算するだけなのに対し、
//   Shaking は「見た目のブレ」を最終座標に直接足すため、
//   Update 側の共通積分（velocity*dt）とは別枠で扱う。
// ============================================================================
inline void DroneMovementSystem::ProcessShaking(TransformComponent &trans, DroneComponent &drone, float dt, float currentTime)
{
    // 本来の陣形移動（velocityの計算＋回転）はそのまま行う
    ProcessMoveToTarget(trans, drone, dt);

    // MoveToTarget が計算した velocity をここで積分する
    // （Update側の共通積分は Shaking を除外しているので、ここで自前で行う）
    trans.position = trans.position + drone.currentVelocity * dt;

    // 震えのオフセットを加算
    float t        = currentTime * 60.0f;
    float shakeAmp = 0.8f;

    float offsetX = sinf(t + drone.localIndex * 1.3f) * shakeAmp;
    float offsetY = cosf(t * 1.1f + drone.localIndex * 0.7f) * shakeAmp;
    float offsetZ = sinf(t * 1.3f + drone.localIndex * 2.1f) * shakeAmp;

    drone.currentShakeOffset = Vector3(offsetX, offsetY, offsetZ);
    trans.position           = trans.position + drone.currentShakeOffset;
}

// L03_PrePhysics で確定させる（PushSystem より前に trans を仕上げる）
REGISTER_LOGIC_SYSTEM(DroneMovementSystem, Priority::LogicStage::L03_PrePhysics);