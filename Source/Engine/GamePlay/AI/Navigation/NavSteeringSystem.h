#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/AI/Navigation/NavAgentComponent.h"

// 経路(waypoints)に従って、物理エンジン(Jolt)経由でAIを動かすシステム
class NavSteeringSystem : public CCL::ECS::IfSystem<NavSteeringSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<NavAgentComponent>> {
public:
    NavSteeringSystem() : IfSystem("NavSteeringSystem") {}
    virtual ~NavSteeringSystem() = default;

    void Update(float dt) override;
};