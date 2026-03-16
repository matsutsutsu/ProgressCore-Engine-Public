#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/RigidBody/JoltRigidBodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

class JoltPushSystem : public CCL::ECS::IfSystem<JoltPushSystem,
                           CCL::ECS::Read<TransformComponent>,
                           CCL::ECS::Read<JoltRigidBodyComponent>> {
  public:
    JoltPushSystem() : IfSystem("JoltPushSystem") {}
    void Update(float dt) override;
};