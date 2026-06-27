#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h" // ★追加

class RootMotionSystem : public CCL::ECS::IfSystem<RootMotionSystem,
                             CCL::ECS::Write<RootMotionComponent>,
                             CCL::ECS::Write<CharacterMovementInputComponent>,
                             CCL::ECS::Read<AnimatorComponent>,
                             CCL::ECS::Read<AnimStateMachineComponent>,
                             CCL::ECS::Read<DashComponent>,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Read<TimeStateComponent>,
                             CCL::ECS::Read<ModelComponent>> // ★追加
{
  public:
    RootMotionSystem()
        : IfSystem("RootMotionSystem")
    {
    }
    void Update(float dt) override;
};