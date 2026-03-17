#include "JoltCharacterUpdateSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void JoltCharacterUpdateSystem::Update(float dt) {
  if (!_world->HasResource<JoltPhysicsManager>())
    return;
  auto &joltManager = _world->GetResource<JoltPhysicsManager>();
  JPH::PhysicsSystem *physicsSystem = joltManager.GetPhysicsSystem();
  JPH::TempAllocator *tempAllocator = joltManager.GetTempAllocator();

  // キャラクターのアップデート設定（重力に対する姿勢制御など）
  JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;

  // 全コアを使って並列でキャラクターの衝突計算を行う
  ForEachParallel([&](TransformComponent &trans,
                      const JoltCharacterHandleComponent &handle) {
    if (!handle.character)
      return;

    // 1. 地形との衝突や段差の乗り越えを計算 (Jolt内部処理)
    handle.character->ExtendedUpdate(
        dt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING), {}, {},
        *tempAllocator);

    // 2. 計算後の「位置」だけをECS側のTransformに同期する
    JPH::RVec3 pos = handle.character->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};

    // ※回転（trans.rotation）は上書きしない！
    // Joltのカプセルは常に直立しており、振り向く処理はPlayerMoveSystem側で行うため。
    trans.isStatic = false; // 行列の再計算を要求
  });
}

// 物理のStepが走る前、または同じPhysicsフェーズで実行する
REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::Physics);