#pragma once
/// @file PlayerInputSystem.h
/// @brief プレイヤーの入力デバイスの状態を読み取り、移動・攻撃の「意思データ」としてコンポーネントに書き込むシステム。
/// @note ハードウェア依存（InputFacade）はすべてこのシステムに集約させる。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"

// ※ 名前空間で囲まない (ガイドライン準拠)
class PlayerInputSystem : public CCL::ECS::IfSystem<
    PlayerInputSystem,
    CCL::ECS::Write<CharacterMovementInputComponent>,
    CCL::ECS::Write<PlayerComponent>>
{
public:
    PlayerInputSystem();
    virtual ~PlayerInputSystem() = default;

    void Update(float dt) override;

};