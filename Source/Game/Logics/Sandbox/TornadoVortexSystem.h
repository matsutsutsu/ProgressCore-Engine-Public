#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "TornadoVortexComponent.h"
#include <cmath>

class TornadoVortexSystem : public CCL::ECS::IfSystem<TornadoVortexSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<TornadoVortexComponent>>
{
public:
    TornadoVortexSystem() : IfSystem("TornadoVortexSystem") {}

    void Update(float dt) override;
};