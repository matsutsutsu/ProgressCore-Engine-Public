#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Prototype/ProtoDroneComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

class ProtoDroneSwarmSystem : public CCL::ECS::IfSystem<ProtoDroneSwarmSystem,
                             CCL::ECS::Write<TransformComponent>,
                             CCL::ECS::Write<ProtoDroneComponent>>
{
  public:
    ProtoDroneSwarmSystem()
        : IfSystem("ProtoDroneSwarmSystem")
    {
    }
    void Update(float dt) override;
};