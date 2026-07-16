// Engine/GamePlay/Physics/Jolt/JoltUpdateSystem.cpp
#include "JoltUpdateSystem.h"
#include <Jolt/Jolt.h>
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void JoltStepSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager*>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager*>();

    // 物理の時間を dt 秒だけ進める（内部でJoltの並列計算が走る）
    joltManager->Step(dt);
}

void JoltPullSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager*>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager*>();

    // ★fix: Step 完了後・誰も物理ボディに書き込まないフェーズなので、
    //   ロックなし BodyInterface を使う（並列読み取り時のロック競合をなくす）。
    //   前提: 本ステージ(L05)で他システムが物理ボディへ書き込まないこと。
    const JPH::BodyInterface &bodyInterface = joltManager->GetBodyInterfaceNoLock();

    // ※キャラクター(JoltCharacterHandleComponent)は SetExclude 済みなので、
    //   ここには流れてこない。旧来の HasComponent 判定は不要になった。
    ForEachWithIDParallel([&](CCL::ECS::EntityID id, TransformComponent& trans, const JoltHandleComponent& joltBody) {

        // Kinematic (アニメーション駆動) の剛体はPullしてはいけない。
        // Kinematicの「正しい座標」は常にECS側(TransformUpdate)にあるため、Joltから上書きさせない。
        if (bodyInterface.GetMotionType(joltBody.bodyID) == JPH::EMotionType::Kinematic) {
            return;
        }

        // スリープ状態（止まっている物体）なら、座標コピーの無駄な処理を省く
        if (!bodyInterface.IsActive(joltBody.bodyID)) {
            return;
        }

        // Joltから最新の座標を取得
        JPH::RVec3 pos;
        JPH::Quat  rot;
        bodyInterface.GetPositionAndRotation(joltBody.bodyID, pos, rot);

        // ECSのTransformを上書き
        trans.position = {pos.GetX(), pos.GetY(), pos.GetZ()};
        trans.rotation = {rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW()};

        // TransformUpdateSystemに変更を知らせて行列を再計算させる
        trans.isStatic = false;
    });
}

// 登録順序がすべてを決定する
REGISTER_LOGIC_SYSTEM(JoltStepSystem, Priority::LogicStage::L04_Physics);
// 物理計算の【後】に同期しなければならない
REGISTER_LOGIC_SYSTEM(JoltPullSystem, Priority::LogicStage::L05_Collision);