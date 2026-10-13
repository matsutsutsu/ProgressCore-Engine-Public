#pragma once
// ===========================================================================
// 概要: 入力デバイスの状態を読み、プレイヤーの移動方向とボタンの「押した瞬間」を Component に書く System。
//
// 責務:
//   - カメラの向きを基準に移動方向を求め、CharacterMovementComponent.desiredVelocity に書く
//   - 各ボタンの「押した瞬間」を判定し、PlayerInputComponent に書く
//   - UI 操作中・カットシーン中・被弾の硬直中は、全入力を中立にする
//
// 備考:
//   プレイヤーの入力を読むのはこの System だけにする（IInputAPI への依存をここに集める）。
//   押した瞬間の判定をここで行う理由は PlayerInputComponent.h を参照。
//   操作を受け付けない理由は3つあるが、結果はどれも「全入力を中立にする」なので、
//   各アクションの System に条件を持たせず、ここでまとめて判定する。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"

class PlayerInputSystem : public CCL::ECS::IfSystem<PlayerInputSystem,
                                                    CCL::ECS::Write<CharacterMovementComponent>,
                                                    CCL::ECS::Write<PlayerInputComponent>>
{
public:
    PlayerInputSystem() : IfSystem("PlayerInputSystem") {}
    virtual ~PlayerInputSystem() = default;

    void Update(float dt) override;
};
