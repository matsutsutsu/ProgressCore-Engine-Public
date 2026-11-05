#pragma once
// ===========================================================================
// 概要: 入力デバイスを読み、プレイヤーの移動の向きと
//       ボタンを押した瞬間を Component に書く System
//
// 責務:
//   - カメラの向きを基準に移動の向きを求め、desiredVelocity に書く
//   - 各ボタンを押した瞬間を判定し、PlayerInputComponent に書く
//   - UI 操作中・演出中・決着後・被弾の硬直中は、全入力を中立にする
//
// 備考:
//   プレイヤーの入力を読むのはこの System だけ（IInputAPI への依存をここに集める）
//   止める理由はいくつかあるが、結果はどれも「全入力を中立にする」なので、
//   各アクションの System には止める条件を持たせない
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"

class PlayerInputSystem : public CCL::ECS::IfSystem<
    PlayerInputSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<PlayerInputComponent>>
{
public:
    PlayerInputSystem() : IfSystem("PlayerInputSystem") {}
    virtual ~PlayerInputSystem() = default;

    void Update(float dt) override;
};
