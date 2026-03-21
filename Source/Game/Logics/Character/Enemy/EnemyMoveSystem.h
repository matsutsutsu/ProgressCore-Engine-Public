#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"

// 剛体(JoltHandle)を直接操作してプレイヤーを追い詰めるシステム
class EnemyMoveSystem
    : public CCL::ECS::IfSystem<
          EnemyMoveSystem, CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Read<EnemyComponent>, CCL::ECS::Read<JoltHandleComponent>> {
public:
  EnemyMoveSystem() : IfSystem("EnemyMoveSystem") {}
  void Update(float dt) override;
};