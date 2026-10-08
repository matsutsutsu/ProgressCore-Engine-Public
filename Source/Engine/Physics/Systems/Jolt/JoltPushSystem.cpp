#include "JoltPushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>

void JoltPushSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>())
        return;
    auto               &joltManager   = m_world->GetResource<JoltPhysicsManager *>();
    JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterface();

    // BodyInterface（ロック付き）への書き込みはボディ単位で排他されるので、並列で回してよい
    ForEachParallel(
        [&](TransformComponent &trans, const JoltHandleComponent &joltBody, const JoltRigidbodyComponent &rigid) {
            // エディタで Dynamic <-> Kinematic を切り替えたら、すぐ Jolt へ反映する
            if (bodyInterface.GetMotionType(joltBody.bodyID) != rigid.motionType)
            {
                bodyInterface.SetMotionType(joltBody.bodyID, rigid.motionType, JPH::EActivation::Activate);
            }

            // ローカルではなくワールドの位置・回転を渡す
            DirectX::XMFLOAT3 worldPos = trans.GetWorldPosition();
            DirectX::XMFLOAT4 worldRot = trans.GetWorldRotation();

            JPH::RVec3 joltPos(worldPos.x, worldPos.y, worldPos.z);
            JPH::Quat  joltRot(worldRot.x, worldRot.y, worldRot.z, worldRot.w);
            joltRot = joltRot.Normalized(); // 計算誤差で長さが 1 からずれた回転を Jolt へ渡さない

            // 強制ワープの要求
            if (trans.isTeleported)
            {
                bodyInterface.SetPositionAndRotation(
                    joltBody.bodyID,
                    joltPos,
                    joltRot,
                    JPH::EActivation::Activate);

                trans.isTeleported = false;
            }
            // Kinematic（プログラム/アニメーション駆動）の剛体は毎ステップ ECS 側の位置へ合わせる
            else
            {
                if (bodyInterface.GetMotionType(joltBody.bodyID) == JPH::EMotionType::Kinematic)
                {
                    // SetPositionAndRotation（瞬間移動）で「遅延ゼロ」に追従させる。
                    //   MoveKinematic は目標到達がステップ末＝1フレーム遅れ、速い攻撃の振りでは
                    //   大きくズレてしまう（遅い移動では当たるのに攻撃で当たらない原因になる）。
                    //   ここが正確でないと接触イベントが正しく発生しないため、位置の正確さを最優先する。
                    bodyInterface.SetPositionAndRotation(
                        joltBody.bodyID, joltPos, joltRot, JPH::EActivation::Activate);
                }
            }
        });
}

// 物理計算の前にワープを完了させる。
//   ただし BoneAttachmentSystem(L03_PrePhysics) が剣などの worldMatrix を
//   ボーン追従で確定させた「後」に読む必要があるため、1段後ろの L03_PushToPhysics に置く。
//   （L03_PrePhysics のままだと BoneAttachment より先に走り、追従前の素の位置を
//     ボディへ焼いてしまい、剣が振りに追従せず攻撃が当たらない。）
REGISTER_LOGIC_SYSTEM(JoltPushSystem, Priority::LogicStage::L03_PushToPhysics);