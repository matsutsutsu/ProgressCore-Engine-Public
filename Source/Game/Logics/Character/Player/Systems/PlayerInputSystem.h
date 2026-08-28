#pragma once
/// @file PlayerInputSystem.h
/// @brief プレイヤーの入力デバイスの状態を読み取り、移動・アクションの「意思データ」として
///        コンポーネントに書き込むシステム。
/// @note ハードウェア依存（InputFacade）はすべてこのシステムに集約させる。
/// @note 「押した瞬間」の解釈もここに集約する。ロジックは同じ描画フレーム内で複数回
///       走ることがあり、各システムが個別にエッジを取ると挙動が揃わないため。
///       詳しい理由は PlayerInputComponent.h を参照。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"


class PlayerInputSystem : public CCL::ECS::IfSystem<PlayerInputSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<PlayerInputComponent>>
{
public:
    PlayerInputSystem();
    virtual ~PlayerInputSystem() = default;

    void Update(float dt) override;
};
