// Engine/GamePlay/Physics/Jolt/JoltUpdateSystem.cpp
#include "JoltUpdateSystem.h"
#include "ECS/Core/CCL_World.h"
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"
#include <Jolt/Jolt.h>

void JoltStepSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    auto &joltManager = _world->GetResource<JoltPhysicsManager>();

    // 物理の時間を dt 秒だけ進める
    joltManager.Step(dt); // ここでJolt内部の並列計算が走る
}

void JoltPullSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;

    auto               &joltManager   = _world->GetResource<JoltPhysicsManager>();
    JPH::BodyInterface &bodyInterface = joltManager.GetBodyInterface();


    ForEachParallel([&](TransformComponent &trans, const JoltHandleComponent &joltBody) {
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
// 物理計算の【後】に同期しなければならないため、新しくPostPhysicsなどのステージを作るか、Collisionの後に配置します
REGISTER_LOGIC_SYSTEM(JoltPullSystem, Priority::LogicStage::L05_Collision);