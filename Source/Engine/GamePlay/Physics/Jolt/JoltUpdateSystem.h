// Engine/GamePlay/Physics/Jolt/JoltUpdateSystem.h
#pragma once
#include <Jolt/Jolt.h>
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"

// 1. 物理の時間を進めるシステム
class JoltStepSystem : public CCL::ECS::IfSystem<JoltStepSystem> {
  public:
    JoltStepSystem() : IfSystem("JoltStepSystem") {}
    void Update(float dt) override;
};

// 2. 計算結果をECSのTransformに同期するシステム
class JoltPullSystem : public CCL::ECS::IfSystem<JoltPullSystem,
                           CCL::ECS::Write<TransformComponent>,
                           CCL::ECS::Read<JoltHandleComponent>> {
  public:
    JoltPullSystem() : IfSystem("JoltPullSystem") {}
    void Update(float dt) override;
};