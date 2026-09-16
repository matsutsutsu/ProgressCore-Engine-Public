#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"

// 経路(waypoints)に従って、物理エンジン(Jolt)経由でAIを動かすシステム
class NavSteeringSystem : public CCL::ECS::IfSystem<NavSteeringSystem,
                                                    CCL::ECS::Write<TransformComponent>,
                                                    CCL::ECS::Write<NavAgentComponent>,
                                                    CCL::ECS::Write<CharacterMovementComponent>>
{
public:
    NavSteeringSystem() : IfSystem("NavSteeringSystem") {}
    virtual ~NavSteeringSystem() = default;

    void Update(float dt) override;
};