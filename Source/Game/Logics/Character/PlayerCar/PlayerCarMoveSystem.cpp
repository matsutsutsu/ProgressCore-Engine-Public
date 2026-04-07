#include "PlayerCarMoveSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include <DirectXMath.h>
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// Joltのレイキャストに必要なヘッダー
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>

using namespace DirectX;
using namespace CCL::ECS;

PlayerCarMoveSystem::PlayerCarMoveSystem() : IfSystem("PlayerCarMoveSystem") {}

void PlayerCarMoveSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto& physicsManager = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface& bodyInterface = physicsManager.GetBodyInterface();

    // レイキャストを撃つためのクエリシステムを取得
    // ※もしGetPhysicsSystem() という関数がない場合は、エラーメッセージを教えてください。
    const JPH::NarrowPhaseQuery& narrowPhase = physicsManager.GetPhysicsSystem()->GetNarrowPhaseQuery();

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
        //  レイキャストによる「接地判定(IsGrounded)」
        // =======================================================
        JPH::RVec3 rayStart(sphereTrans->position.x, sphereTrans->position.y, sphereTrans->position.z);
        JPH::RVec3 rayDir(0.0f, -car.raycastLength, 0.0f); // 真下に撃つ

        JPH::RayCastResult hit;
        // 自分自身（車の球体）に当たった場合は無視するフィルター
        JPH::IgnoreSingleBodyFilter ignoreSelf(sphereHandle->bodyID);

        // レーザーを発射！
        car.isGrounded = narrowPhase.CastRay(
            JPH::RRayCast(rayStart, rayDir),
            hit,
            JPH::BroadPhaseLayerFilter(), // 全ての層を対象
            JPH::ObjectLayerFilter(),     // 全ての層を対象
            ignoreSelf                    // 自分は無視
        );

        // =======================================================
        // 3. フェイク・サスペンション（慣性の計算）
        // =======================================================
        float targetPitch = 0.0f;
        float targetRoll = 0.0f;

        // ★ 地面にいる時だけ傾く。空中の時は目標値を0(水平)にする！
        if (car.isGrounded) {
            targetPitch = car.input.throttle * -car.maxPitchAngle;
            targetRoll = car.input.steering * -car.maxRollAngle;
        }

        // 線形補間(Lerp)で、現在の傾きを目標値に向かって滑らかに追従させる（バネの表現）
        car.currentPitch += (targetPitch - car.currentPitch) * car.suspensionSpeed * dt;
        car.currentRoll  += (targetRoll  - car.currentRoll)  * car.suspensionSpeed * dt;

        // =======================================================
        // 4. 描画用の位置と回転を計算（オフセットとサスの完全合成）
        // =======================================================
        
        // [位置]
        XMVECTOR offsetVec = XMLoadFloat3(&car.visualOffset);
        offsetVec = XMVector3Rotate(offsetVec, logicRot);
        XMVECTOR spherePos = XMLoadFloat3(&sphereTrans->position);
        XMStoreFloat3(&visualTrans.position, XMVectorAdd(spherePos, offsetVec));

        // [回転] visualRotation(ベース) に currentPitch(動的) を足す！
        // X軸回転 = Pitch(前後), Z軸回転 = Roll(左右) 
        // ※モデルのデフォルトの向きによってはXZが逆になる場合があります。違和感があれば入れ替えてください。
        XMVECTOR offsetRot = XMQuaternionRotationRollPitchYaw(
            XMConvertToRadians(car.visualRotation.x + car.currentPitch), 
            XMConvertToRadians(car.visualRotation.y),
            XMConvertToRadians(car.visualRotation.z + car.currentRoll)   
        );
        
        // ローカルの傾きを、車の論理的な向き(logicRot)に掛け合わせる
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
      // 接地している時（Grounded）だけ実行する！
        if (car.isGrounded) {

            // 6. アクセル（力計算）
            if (car.input.throttle != 0.0f) {
                XMFLOAT3 forward;
                XMStoreFloat3(&forward, forwardVec);
                float finalForce = car.acceleration * 100.0f;
                JPH::Vec3 force(forward.x * car.input.throttle * finalForce, 0.0f, forward.z * car.input.throttle * finalForce);
                bodyInterface.AddForce(sphereHandle->bodyID, force);
                bodyInterface.ActivateBody(sphereHandle->bodyID);
            }

            // 7. ドリフトキャンセラー
            JPH::Vec3 currentVelJolt = bodyInterface.GetLinearVelocity(sphereHandle->bodyID);
            XMVECTOR currentVel = XMVectorSet(currentVelJolt.GetX(), currentVelJolt.GetY(), currentVelJolt.GetZ(), 0.0f);
            float rightSpeed = XMVectorGetX(XMVector3Dot(currentVel, rightVec));

            XMVECTOR cancelVel = XMVectorScale(rightVec, -rightSpeed * 0.95f);
            JPH::Vec3 newVel(
                currentVelJolt.GetX() + XMVectorGetX(cancelVel),
                currentVelJolt.GetY(), // Y(重力)はそのまま
                currentVelJolt.GetZ() + XMVectorGetZ(cancelVel)
            );
            bodyInterface.SetLinearVelocity(sphereHandle->bodyID, newVel);
        }
        // 空中の場合は何も加算・減速しない（Joltの重力と慣性に完全に身を任せる）
        });
}

REGISTER_LOGIC_SYSTEM(PlayerCarMoveSystem, Priority::LogicStage::L02_Update);