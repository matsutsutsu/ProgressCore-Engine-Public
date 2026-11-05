#pragma once
// ===========================================================================
// 概要: 物理の時間を進める System と、その結果を Transform へ書き戻す System
//
// 責務:
//   - JoltStepSystem（L04_Physics）: JoltPhysicsManager::Step() で物理を dt 秒進める
//   - JoltPullSystem（L05_Collision）: 動いた剛体の位置・回転を Transform へ書き戻す
//
// 備考: CharacterVirtual を持つ Entity は JoltCharacterUpdateSystem が同期済みなので、
//       JoltPullSystem は SetExclude で Chunk ごと除外する
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

// 物理の時間を進める
class JoltStepSystem : public CCL::ECS::IfSystem<
    JoltStepSystem>
{
public:
    JoltStepSystem() : IfSystem("JoltStepSystem") {}
    void Update(float dt) override;
};

// 物理の結果を Transform へ書き戻す
class JoltPullSystem : public CCL::ECS::IfSystem<
    JoltPullSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<JoltHandleComponent>>
{
public:
    JoltPullSystem() : IfSystem("JoltPullSystem")
    {
        // キャラクターは JoltCharacterUpdateSystem が既に位置を同期している
        // ここでも書くと二重に書き込んでカクつくため、Chunk ごと対象から外す
        SetExclude<JoltCharacterHandleComponent>();
    }
    void Update(float dt) override;
};