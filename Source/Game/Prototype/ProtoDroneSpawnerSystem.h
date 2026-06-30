#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"

class ProtoDroneSpawnerSystem : public CCL::ECS::IfSystem<ProtoDroneSpawnerSystem>
{
  public:
    ProtoDroneSpawnerSystem()
        : IfSystem("ProtoDroneSpawnerSystem")
    {
    }
    void Update(float dt) override;
};