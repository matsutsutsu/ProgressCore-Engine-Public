#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

class PlayerDashSystem : public CCL::ECS::IfSystem<
    PlayerDashSystem,
    CCL::ECS::Write<CharacterMovementInputComponent>,
    CCL::ECS::Write<DashComponent>,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<TimeStateComponent>>
{
public:
    PlayerDashSystem() : IfSystem("PlayerDashSystem") {}
    void Update(float dt) override;
};