#pragma once
/// @file PlayerAnimParamSyncSystem.h
/// @brief ゲーム固有の状態を、アニメの「掲示板」(AnimParameters) へ転記するシステム。
///
/// 位置付け:
///   物理(Jolt)由来の Speed / YVelocity / IsGrounded は Engine 側の
///   CharacterAnimSyncSystem が担当する。こちらは「ダッシュ中か」のような
///   ゲームのルールに属する状態だけを受け持つ。
///
/// なぜ分けるか:
///   Engine のシステムが DashComponent(Game) を読みに行くと Engine→Game 依存に
///   なるため、ゲーム側の状態はゲーム側から掲示板へ push する。
///
/// 実行ステージ:
///   L02_PreUpdate。掲示板を消費する AnimControllerSystem(L02_Update) より
///   確実に前に走らせる必要がある（同一ステージだと順序が登録順依存になるため）。
///   参照する DashComponent は前フレームの値で、これは移管前の
///   CharacterAnimSyncSystem(L02_PreUpdate) と同じタイミング＝挙動は不変。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

class PlayerAnimParamSyncSystem
    : public CCL::ECS::IfSystem<PlayerAnimParamSyncSystem,
          CCL::ECS::Read<DashComponent>,
          CCL::ECS::Read<PlayerComponent>,
          CCL::ECS::Read<CharacterMovementComponent>,
          CCL::ECS::Write<AnimParametersComponent>>
{
  public:
    PlayerAnimParamSyncSystem()
        : IfSystem("PlayerAnimParamSyncSystem")
    {
    }
    virtual ~PlayerAnimParamSyncSystem() = default;

    void Update(float dt) override;
};
