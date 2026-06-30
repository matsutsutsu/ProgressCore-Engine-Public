#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Prototype/ProtoDroneComponent.h"

class ProtoCombatSystem : public CCL::ECS::IfSystem<ProtoCombatSystem,
                              CCL::ECS::Read<TransformComponent>,
                              CCL::ECS::Read<ProtoDroneComponent>>
{
  public:
    ProtoCombatSystem()
        : IfSystem("ProtoCombatSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;
};