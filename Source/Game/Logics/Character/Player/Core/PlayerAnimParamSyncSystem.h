#pragma once
// ===========================================================================
// 概要: プレイヤーのゲーム側の状態を、アニメのパラメータへ書く System
//
// 責務:
//   - IsDashing / IsAirborne / MoveInput / IsRunning を書き、ダッシュを始めた瞬間に OnDash を立てる
//   - 次の攻撃をダッシュ攻撃にするかを判定し、DashAttackReady を書く
//
// 備考:
//   物理から求める Speed / YVelocity / IsGrounded は、Engine の CharacterAnimSyncSystem が書く
//   こちらはゲームのルールに属する状態だけを書く（Engine から Game の Component を読まないため）
//
//   実行ステージは L02_PreUpdate。パラメータを読む AnimControllerSystem(L02_Update) より前
//   PlayerDashComponent は、PlayerDashSystem(L02_Update) が前のステップで書いた値を読む
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

class PlayerAnimParamSyncSystem : public CCL::ECS::IfSystem<
    PlayerAnimParamSyncSystem,
    CCL::ECS::Read<PlayerDashComponent>,
    CCL::ECS::Read<PlayerComponent>,
    CCL::ECS::Read<CharacterMovementComponent>,
    CCL::ECS::Write<AnimParametersComponent>>
{
public:
    PlayerAnimParamSyncSystem() : IfSystem("PlayerAnimParamSyncSystem") {}
    virtual ~PlayerAnimParamSyncSystem() = default;

    void Update(float dt) override;
};
