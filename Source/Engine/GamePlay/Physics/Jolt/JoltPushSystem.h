#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include "Engine/GamePlay/Physics/Jolt/JoltTeleportRequest.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

class JoltPushSystem : public CCL::ECS::IfSystem<JoltPushSystem,
                           CCL::ECS::Read<JoltHandleComponent>,
                           CCL::ECS::Read<JoltTeleportRequest>> {
  public:
    JoltPushSystem() : IfSystem("JoltPushSystem") {}
    void Update(float dt) override;
};