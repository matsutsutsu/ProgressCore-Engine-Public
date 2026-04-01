#include "JoltTransformSyncSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void JoltTransformSyncSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    JPH::BodyInterface& bodyInterface = _world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    ForEach([&](TransformComponent& trans, const JoltHandleComponent& handle) {

        // ★ エディタやスクリプトから「ワープ命令」が出ているか？
        if (!trans.isTeleported) return;

        // Jolt用の座標と回転に変換
        JPH::RVec3 newPos(trans.position.x, trans.position.y, trans.position.z);
        JPH::Quat  newRot(trans.rotation.x, trans.rotation.y, trans.rotation.z, trans.rotation.w);

        // ★ 外科手術：強制ワープ！ (StaticなMeshColliderであっても強制的に動かす)
        // 第4引数を Activate にすることで、動かした先にいた他の剛体を「物理的に叩き起こす」
        bodyInterface.SetPositionAndRotation(handle.bodyID, newPos, newRot, JPH::EActivation::Activate);

        // ワープ完了。フラグを折る。
        trans.isTeleported = false;
        });
}

// 物理計算が始まる「直前」にワープを終わらせておく
REGISTER_LOGIC_SYSTEM(JoltTransformSyncSystem, Priority::LogicStage::PrePhysics);