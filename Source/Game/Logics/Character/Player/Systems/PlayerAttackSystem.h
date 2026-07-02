#pragma once
/// @file PlayerAttackSystem.h
/// @brief プレイヤーの入力意思を評価し、アニメーションの先行入力バッファ（トリガー）を登録するシステム
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class PlayerAttackSystem : public CCL::ECS::IfSystem<
                               PlayerAttackSystem,
                               CCL::ECS::Read<CharacterMovementInputComponent>,
                               CCL::ECS::Write<AnimParametersComponent>,
                               CCL::ECS::Write<PlayerComponent>>
{
  public:
    PlayerAttackSystem()
        : IfSystem("PlayerAttackSystem")
    {
    }
    virtual ~PlayerAttackSystem() = default;

    void Update(float dt) override;
};