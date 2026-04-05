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

    ForEach([&](TransformComponent& visualTrans, const PlayerCarComponent& car) {

        // =======================================================
        // 1. ハンドル操作（車体を直接回転させる）
        // =======================================================
        if (car.input.steering != 0.0f) {
            float turnRadian = XMConvertToRadians(car.input.steering * car.turnSpeed * dt);
            XMVECTOR currentRot = XMLoadFloat4(&visualTrans.rotation);
            XMVECTOR turnRot = XMQuaternionRotationAxis(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), turnRadian);

            currentRot = XMQuaternionMultiply(currentRot, turnRot);
            XMStoreFloat4(&visualTrans.rotation, currentRot);
        }

        // =======================================================
        // 2. 物理球体との同期
        // =======================================================
        if (!_world->IsEntityValid(car.physicsSphereID)) return;
        auto* sphereTrans = _world->GetComponent<TransformComponent>(car.physicsSphereID);
        auto* sphereHandle = _world->GetComponent<JoltHandleComponent>(car.physicsSphereID);
        if (!sphereTrans || !sphereHandle) return;

        visualTrans.position = sphereTrans->position;

        // ★修正: 球の半径(1.0f)分だけ車体のY座標を下げて、タイヤを地面にピタッと接地させる！
        visualTrans.position.y -= 1.0f;

        // =======================================================
        // 3. 車の向きベクトルの計算 (水平に固定)
        // =======================================================
        XMVECTOR rot = XMLoadFloat4(&visualTrans.rotation);

        XMVECTOR forwardVec = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rot); // Z+が前方
        forwardVec = XMVectorSetY(forwardVec, 0.0f); // ★修正: 空を飛んだり地中に潜るのを防ぐ
        forwardVec = XMVector3Normalize(forwardVec);

        XMVECTOR rightVec = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rot);   // X+が右方
        rightVec = XMVectorSetY(rightVec, 0.0f);
        rightVec = XMVector3Normalize(rightVec);

        // =======================================================
        // 4. アクセル操作（めっちゃ遅い問題の解決）
        // =======================================================
        if (car.input.throttle != 0.0f) {
            XMFLOAT3 forward;
            XMStoreFloat3(&forward, forwardVec);

            // ★修正: 質量に対して力が足りないので、100倍にスケールアップ！
            float finalForce = car.acceleration * 100.0f;

            JPH::Vec3 force(
                forward.x * car.input.throttle * finalForce,
                0.0f,
                forward.z * car.input.throttle * finalForce
            );
            bodyInterface.AddForce(sphereHandle->bodyID, force);
            bodyInterface.ActivateBody(sphereHandle->bodyID);
        }

        // =======================================================
        // 5. アーケード挙動（ドリフト・キャンセラー）
        // =======================================================
        // 現在の球体の速度を取得
        JPH::Vec3 currentVelJolt = bodyInterface.GetLinearVelocity(sphereHandle->bodyID);
        XMVECTOR currentVel = XMVectorSet(currentVelJolt.GetX(), currentVelJolt.GetY(), currentVelJolt.GetZ(), 0.0f);

        // 車体の「右方向（横滑り）」の速度成分を抽出
        float rightSpeed = XMVectorGetX(XMVector3Dot(currentVel, rightVec));

        // ★ 横滑りの速度を 95% 打ち消す（5%だけ残すことで、気持ちいいドリフト感になる）
        XMVECTOR cancelVel = XMVectorScale(rightVec, -rightSpeed * 0.95f);

        JPH::Vec3 newVel(
            currentVelJolt.GetX() + XMVectorGetX(cancelVel),
            currentVelJolt.GetY(), // Y(上下・重力)の速度はそのまま維持
            currentVelJolt.GetZ() + XMVectorGetZ(cancelVel)
        );

        bodyInterface.SetLinearVelocity(sphereHandle->bodyID, newVel);
        });
}

REGISTER_LOGIC_SYSTEM(PlayerCarMoveSystem, Priority::LogicStage::L02_Update);