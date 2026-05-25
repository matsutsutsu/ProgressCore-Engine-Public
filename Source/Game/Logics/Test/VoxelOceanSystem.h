#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "VoxelOceanComponent.h"
#include <cmath>

class VoxelOceanSystem : public CCL::ECS::IfSystem<VoxelOceanSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<VoxelOceanComponent>> {
public:
    VoxelOceanSystem() : IfSystem("VoxelOceanSystem") {}

    void Update(float dt) override;
};