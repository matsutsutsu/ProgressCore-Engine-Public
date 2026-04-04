#include "JoltCharacterUpdateSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include <Jolt/Physics/Body/BodyFilter.h>

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void JoltCharacterUpdateSystem::Update(float dt) {
    if (!_world->HasResource<JoltPhysicsManager>())
      return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager>();
    JPH::PhysicsSystem *physicsSystem = joltManager.GetPhysicsSystem();
    JPH::TempAllocator *tempAllocator = joltManager.GetTempAllocator();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();

    // キャラクターのアップデート設定（重力に対する姿勢制御など）
    JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;

    // 全コアを使って並列でキャラクターの衝突計算を行う
    ForEachWithIDParallel([&](CCL::ECS::EntityID id, TransformComponent &trans,
                              const JoltCharacterHandleComponent &handle) {
    if (!handle.character) return;

    // =======================================================
    // プレイヤーの強制ワープ処理
    // =======================================================
    if (trans.isTeleported) {
      handle.character->SetPosition(
          JPH::RVec3(trans.position.x, trans.position.y, trans.position.z));
      handle.character->SetRotation(
          JPH::Quat(trans.rotation.x, trans.rotation.y, trans.rotation.z,
                    trans.rotation.w));

      // フラグを下ろす
      trans.isTeleported = false;
    }
 

    // =======================================================
    // 自分自身の分身（Kinematic剛体）を無視するフィルターを作成
    // =======================================================
    JPH::BodyID ignoreBodyID;
    if (auto *kinBody = _world->GetComponent<JoltHandleComponent>(id)) {
      ignoreBodyID = kinBody->bodyID;
    }
    JPH::IgnoreSingleBodyFilter bodyFilter(ignoreBodyID);


    // 1. 地形との衝突や段差の乗り越えを計算 (Jolt内部処理)
    handle.character->ExtendedUpdate(
        dt, physicsSystem->GetGravity(), updateSettings,
        physicsSystem->GetDefaultBroadPhaseLayerFilter(PhysicsLayers::MOVING),
        physicsSystem->GetDefaultLayerFilter(PhysicsLayers::MOVING),
        bodyFilter,
        {},
        *tempAllocator);

    // 2. 計算後の「位置」だけをECS側のTransformに同期する
    JPH::RVec3 pos = handle.character->GetPosition();
    trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};

    // ※回転（trans.rotation）は上書きしない！
    // Joltのカプセルは常に直立しており、振り向く処理はPlayerMoveSystem側で行うため。
    trans.isStatic = false; // 行列の再計算を要求

    // =======================================================
    // プレイヤーの「幽霊」を実体化させる同期処理
    // =======================================================
    if (auto* kinBody = _world->GetComponent<JoltHandleComponent>(id)) {
        // CharacterVirtualの最新座標を、当たり判定用の分身(Kinematic剛体)にも流し込む
        bodyInterface.SetPosition(kinBody->bodyID, pos, JPH::EActivation::DontActivate);
    }
  });
}

// 物理のStepが走る前、または同じPhysicsフェーズで実行する
REGISTER_LOGIC_SYSTEM(JoltCharacterUpdateSystem, Priority::LogicStage::L03_PrePhysics);