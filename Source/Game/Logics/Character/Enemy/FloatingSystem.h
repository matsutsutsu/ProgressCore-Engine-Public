#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/RigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Game/Logics/Character/Enemy/FloatingComponent.h"

class FloatingSystem : public CCL::ECS::IfSystem<FloatingSystem,
                           CCL::ECS::Write<TransformComponent>,
                           CCL::ECS::Write<RigidBodyComponent>,
                           CCL::ECS::Write<FloatingComponent>> {
  public:
    FloatingSystem();
    virtual ~FloatingSystem() = default;

    void Update(float dt) override;
};