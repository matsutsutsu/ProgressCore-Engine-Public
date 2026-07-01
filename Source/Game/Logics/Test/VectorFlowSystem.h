#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "VectorFlowComponent.h"
#include <cmath>

class VectorFlowSystem : public CCL::ECS::IfSystem<VectorFlowSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<VectorFlowComponent>>
{
public:
    VectorFlowSystem() : IfSystem("VectorFlowSystem") {}

    void Update(float dt) override;
};