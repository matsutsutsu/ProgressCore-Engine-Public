#include "JoltUpdateSystem.h"
#include <Jolt/Jolt.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Transform/Systems/TransformUpdateSystem.h"

void JoltStepSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    auto &joltManager = m_world->GetResource<JoltPhysicsManager *>();

    // 物理の時間を dt 秒だけ進める（中で Jolt のジョブが並列に走る）
    joltManager->Step(dt);
}



void JoltPullSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    auto &joltManager = m_world->GetResource<JoltPhysicsManager *>();

    // Step 完了後で、誰も物理ボディに書き込まないフェーズなので、
    //   ロックなしの BodyInterface を使う（並列読み取り時のロック競合をなくす）
    //   前提: 本ステージ(L05)で他システムが物理ボディへ書き込まないこと
    const JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterfaceNoLock();

    // キャラクター(JoltCharacterHandleComponent)は SetExclude 済みなので、ここには来ない
    ForEachWithIDParallel([&](CCL::ECS::EntityID id, TransformComponent &trans, const JoltHandleComponent &joltBody) {
        // Kinematic（アニメーション駆動）の剛体は書き戻さない
        // Kinematic の正しい位置は常に ECS 側にあるため、Jolt の値で上書きしない
        if (bodyInterface.GetMotionType(joltBody.bodyID) == JPH::EMotionType::Kinematic)
        {
            return;
        }

        // スリープ中（止まっている）なら書き戻す必要がない
        if (!bodyInterface.IsActive(joltBody.bodyID))
        {
            return;
        }

        JPH::RVec3 pos;
        JPH::Quat  rot;
        bodyInterface.GetPositionAndRotation(joltBody.bodyID, pos, rot);

        // Transform を上書きする
        //   Jolt が返すのはワールド値、position/rotation はローカル欄なので、
        //   親を持つEntityでは変換が要る（中で isStatic=false も立てる）
        TransformUpdateSystem::WriteWorldTransform(*m_world, id, trans,
                                                   {pos.GetX(), pos.GetY(), pos.GetZ()},
                                                   {rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW()});
    });
}



REGISTER_LOGIC_SYSTEM(JoltStepSystem, Priority::LogicStage::L04_Physics);
// 物理計算の後に書き戻す
REGISTER_LOGIC_SYSTEM(JoltPullSystem, Priority::LogicStage::L05_Collision);