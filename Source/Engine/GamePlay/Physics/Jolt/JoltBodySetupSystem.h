#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Physics/RigidBody/JoltRigidbodyComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

// 物理の「設計図」から「実体」を自動生成するファクトリーシステム
class JoltBodySetupSystem : public CCL::ECS::IfSystem<JoltBodySetupSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Read<JoltRigidbodyComponent>> {
  public:
    JoltBodySetupSystem() : IfSystem("JoltBodySetupSystem") {}
    virtual ~JoltBodySetupSystem() = default;

    void Update(float dt) override;
};