#include "JoltPushSystem.h"
#include "ECS/Core/CCL_World.h"
#include <DirectXMath.h>

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void JoltPushSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto               &joltManager   = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // ★あなたの強力なJobSystemを使って全コアで並列検知を行う
    ForEachParallel([&](const TransformComponent &trans, const JoltRigidBodyComponent &joltBody) {
        // Jolt側の現在の座標を取得
        JPH::RVec3 joltPos;
        JPH::Quat  joltRot;
        bodyInterface.GetPositionAndRotation(joltBody.bodyID, joltPos, joltRot);

        // ECS側の座標とJolt側の座標の「距離の二乗（高速な比較）」を計算
        DirectX::XMVECTOR ecsP =
            DirectX::XMVectorSet(trans.position.x, trans.position.y, trans.position.z, 0.0f);
        DirectX::XMVECTOR joltP =
            DirectX::XMVectorSet(joltPos.GetX(), joltPos.GetY(), joltPos.GetZ(), 0.0f);

        DirectX::XMVECTOR distSqVec =
            DirectX::XMVector3LengthSq(DirectX::XMVectorSubtract(ecsP, joltP));
        float distSq;
        DirectX::XMStoreFloat(&distSq, distSqVec);

        // 許容誤差（0.0001f =
        // 1cmのズレ）を超えていたら、ゲームロジックによって強制移動させられたと判定
        const float DISTANCE_THRESHOLD_SQ = 0.0001f;

        if (distSq > DISTANCE_THRESHOLD_SQ) {
            // ECSの座標でJoltの剛体を上書き（テレポート）する
            bodyInterface.SetPositionAndRotation(joltBody.bodyID,
                JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
                JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w),
                JPH::EActivation::
                    Activate // ★重要: ワープしたのだからスリープを強制解除して物理計算を再開させる
            );
        }
    });
}

// ======================================================================
// ★最重要: 登録順序
// このPush(伝達)は、AIやプレイヤー入力(PrePhysics)の直後であり、
// JoltがStep(計算)を始める【前】に実行されなければならない。
// ======================================================================
REGISTER_LOGIC_SYSTEM(JoltPushSystem, Priority::LogicStage::PrePhysics);