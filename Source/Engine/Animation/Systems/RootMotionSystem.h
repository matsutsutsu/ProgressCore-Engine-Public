// RootMotionSystem.h
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"

/// @brief rootMotionCurve を評価し forward 前進を overrideSpeed 機構へ流すシステム。
/// @details 現ステートの applyRootMotion を AnimStateMachineComponent から直接判定する
///          （AnimController を介さないので並列安全・無依存）。Dash 中は譲る。
class RootMotionSystem : public CCL::ECS::IfSystem<RootMotionSystem,
                             CCL::ECS::Write<RootMotionComponent>,
                             CCL::ECS::Write<CharacterMovementInputComponent>,
                             CCL::ECS::Read<AnimatorComponent>,
                             CCL::ECS::Read<AnimStateMachineComponent>,
                             CCL::ECS::Read<DashComponent>,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Read<TimeStateComponent>>
{
  public:
    RootMotionSystem()
        : IfSystem("RootMotionSystem")
    {
    }
    void Update(float dt) override;
};