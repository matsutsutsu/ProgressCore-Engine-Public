#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Game/Logics/World/StageMap.h"

// 壁埋まり救済システム
// Transformを持つすべてのエンティティ（プレイヤー・敵）を監視します
class StruckRescueSystem : public CCL::ECS::IfSystem<StruckRescueSystem,
                               CCL::ECS::Write<TransformComponent>,
                               CCL::ECS::Write<RigidBodyComponent>> {
  public:
    StruckRescueSystem() : IfSystem("StruckRescueSystem") {}

    void Update(float dt) override;
};