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

    // 削除予定のエンティティIDを記録するリスト
    std::vector<CCL::ECS::EntityID> completedRequests;

    // ★手紙(TeleportRequest)を持っているエンティティだけがここに流れてくる（超高速）
    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &joltBody, const JoltTeleportRequest &request) {
       
        // Joltの剛体を指定の座標へ強制ワープさせる
        bodyInterface.SetPosition(
            joltBody.bodyID,
            JPH::RVec3(request.targetPosition.x, request.targetPosition.y, request.targetPosition.z),
            JPH::EActivation::Activate // ワープ後はスリープを解除
        );

        // 処理が終わったのでリストに追加
        completedRequests.push_back(id);
    });

    // 処理済みのエンティティから手紙(コンポーネント)を剥がす（次フレーム以降は無視される）
    for (auto id : completedRequests) {
      _world->RequestRemoveComponent<JoltTeleportRequest>(id);
    }
}

// ======================================================================
// ★最重要: 登録順序
// このPush(伝達)は、AIやプレイヤー入力(PrePhysics)の直後であり、
// JoltがStep(計算)を始める【前】に実行されなければならない。
// ======================================================================
REGISTER_LOGIC_SYSTEM(JoltPushSystem, Priority::LogicStage::PrePhysics);