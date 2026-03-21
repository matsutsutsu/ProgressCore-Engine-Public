#include "JoltPushSystem.h"
#include "ECS/Core/CCL_World.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void JoltPushSystem::Update(float dt) {
  if (!_world->HasResource<JoltPhysicsManager>())
    return;
  auto &joltManager = _world->GetResource<JoltPhysicsManager>();
  JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

  // 並列処理で全エンティティを最速チェック
  ForEachParallel(
      [&](TransformComponent &trans, const JoltHandleComponent &joltBody) {
        // ★フラグが立っているものだけワープを実行
        if (trans.isTeleported) {
          bodyInterface.SetPositionAndRotation(
              joltBody.bodyID,
              JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
              JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z,
                        trans.rotation.w),
              JPH::EActivation::Activate);

          // ワープが済んだらフラグを下ろす（以降のフレームは無視される）
          trans.isTeleported = false;
        }
      });
}

// ★物理計算の前にワープを完了させる
REGISTER_LOGIC_SYSTEM(JoltPushSystem, Priority::LogicStage::PrePhysics);