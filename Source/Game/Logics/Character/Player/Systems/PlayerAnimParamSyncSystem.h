#pragma once
// ===========================================================================
// 概要: プレイヤーのゲーム側の状態を、アニメーションのパラメータ(AnimParameters)へ書く System。
//
// 責務:
//   - IsDashing / IsAirborne / MoveInput を書く
//   - ダッシュ攻撃にするかを判定し、DashAttackReady を書く
//
// 備考:
//   物理(Jolt)から求める Speed / YVelocity / IsGrounded は、Engine 側の
//   CharacterAnimSyncSystem が書く。こちらは「ダッシュ中か」のようなゲームのルールに属する状態だけを書く。
//   Engine の System が DashComponent(Game) を読むと Engine→Game の依存になるため、ゲーム側から書き込む。
//
//   実行ステージは L02_PreUpdate。AnimParameters を読む AnimControllerSystem(L02_Update) より
//   前に走らせる（同じステージだと順序が登録順で決まってしまうため）。
//   読む DashComponent は、PlayerDashSystem(L02_Update) が前のフレームで書いた値になる。
// ===========================================================================

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
