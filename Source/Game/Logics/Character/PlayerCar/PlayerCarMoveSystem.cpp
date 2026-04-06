#include "PlayerCarMoveSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include <DirectXMath.h>
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

using namespace DirectX;
using namespace CCL::ECS;

PlayerCarMoveSystem::PlayerCarMoveSystem() : IfSystem("PlayerCarMoveSystem") {}

void PlayerCarMoveSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    JPH::BodyInterface& bodyInterface = _world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    // ★注意: car から const を外しています
    ForEach([&](TransformComponent& visualTrans, PlayerCarComponent& car) {

        // =======================================================
        // 0. 初回のみ、初期の回転を論理回転として記憶する
        // =======================================================
        if (!car.isLogicRotInitialized) {
            car.logicRotation = visualTrans.rotation;
            car.isLogicRotInitialized = true;
        }

        // =======================================================
        // 1. ハンドル操作（論理的な車体の向きを更新する）
        // =======================================================
        XMVECTOR logicRot = XMLoadFloat4(&car.logicRotation);

        if (car.input.steering != 0.0f) {
            float turnRadian = XMConvertToRadians(car.input.steering * car.turnSpeed * dt);
            XMVECTOR turnRot = XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), turnRadian);

            logicRot = XMQuaternionMultiply(logicRot, turnRot);
            XMStoreFloat4(&car.logicRotation, logicRot);
        }

        // =======================================================
        // 2. 物理球体との同期
        // =======================================================
        if (!_world->IsEntityValid(car.physicsSphereID)) return;
        auto* sphereTrans = _world->GetComponent<TransformComponent>(car.physicsSphereID);
        auto* sphereHandle = _world->GetComponent<JoltHandleComponent>(car.physicsSphereID);
        if (!sphereTrans || !sphereHandle) return;

        // =======================================================
        // 3. 描画用の位置と回転を計算（ローカル・オフセットの完全適用）
        // =======================================================

        // [位置] オフセットを「車の向き(logicRot)」に合わせて回転させてから加算
        XMVECTOR offsetVec = XMLoadFloat3(&car.visualOffset);
        offsetVec = XMVector3Rotate(offsetVec, logicRot);

        XMVECTOR spherePos = XMLoadFloat3(&sphereTrans->position);
        XMVECTOR finalPos = XMVectorAdd(spherePos, offsetVec);
        XMStoreFloat3(&visualTrans.position, finalPos);

        // [回転] オフセット回転を論理回転に合成して「見た目の回転」とする
        XMVECTOR offsetRot = XMQuaternionRotationRollPitchYaw(
            XMConvertToRadians(car.visualRotation.x),
            XMConvertToRadians(car.visualRotation.y),
            XMConvertToRadians(car.visualRotation.z)
        );
        XMVECTOR finalRot = XMQuaternionMultiply(offsetRot, logicRot);
        XMStoreFloat4(&visualTrans.rotation, finalRot);

        // =======================================================
        // 4. 車の向きベクトルの計算 (必ず論理的な向きを基準にする！)
        // =======================================================
        XMVECTOR forwardVec = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), logicRot); // Z+が前方
        forwardVec = XMVectorSetY(forwardVec, 0.0f);
        forwardVec = XMVector3Normalize(forwardVec);

        XMVECTOR rightVec = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), logicRot);   // X+が右方
        rightVec = XMVectorSetY(rightVec, 0.0f);
        rightVec = XMVector3Normalize(rightVec);

        // =======================================================
        // 5. アクセル操作（力計算）
        // =======================================================
        if (car.input.throttle != 0.0f) {
            XMFLOAT3 forward;
            XMStoreFloat3(&forward, forwardVec);

            float finalForce = car.acceleration * 100.0f;
            JPH::Vec3 force(
                forward.x * car.input.throttle * finalForce, 0.0f, forward.z * car.input.throttle * finalForce
            );
            bodyInterface.AddForce(sphereHandle->bodyID, force);
            bodyInterface.ActivateBody(sphereHandle->bodyID);
        }

        // =======================================================
        // 6. アーケード挙動（ドリフト・キャンセラー）
        // =======================================================
        JPH::Vec3 currentVelJolt = bodyInterface.GetLinearVelocity(sphereHandle->bodyID);
        XMVECTOR currentVel = XMVectorSet(currentVelJolt.GetX(), currentVelJolt.GetY(), currentVelJolt.GetZ(), 0.0f);

        float rightSpeed = XMVectorGetX(XMVector3Dot(currentVel, rightVec));

        // 横滑りの速度を 95% 打ち消す
        XMVECTOR cancelVel = XMVectorScale(rightVec, -rightSpeed * 0.95f);

        JPH::Vec3 newVel(
            currentVelJolt.GetX() + XMVectorGetX(cancelVel),
            currentVelJolt.GetY(),
            currentVelJolt.GetZ() + XMVectorGetZ(cancelVel)
        );

        bodyInterface.SetLinearVelocity(sphereHandle->bodyID, newVel);
        });
}

REGISTER_LOGIC_SYSTEM(PlayerCarMoveSystem, Priority::LogicStage::L02_Update);