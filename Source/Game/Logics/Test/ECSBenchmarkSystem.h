#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "OrbitBenchmarkComponent.h"
#include <cmath>

class ECSBenchmarkSystem : public CCL::ECS::IfSystem<ECSBenchmarkSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<OrbitBenchmarkComponent>> {
public:
    ECSBenchmarkSystem() : IfSystem("ECSBenchmarkSystem") {}

    void Update(float dt) override;
};

