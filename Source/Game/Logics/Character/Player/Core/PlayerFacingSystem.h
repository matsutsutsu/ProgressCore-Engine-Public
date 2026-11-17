#pragma once
// ===========================================================================
// 概要: プレイヤーの向き（旋回・傾き）を処理する System
//
// 責務:
//   - 移動の向き（攻撃時は敵の向き）へ向き直り、旋回時の傾きを付ける
//   - 傾きを含む向きを Transform に、yaw だけを Jolt に書く
//
// 備考: 空中の判定とジャンプは PlayerJumpSystem
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

class PlayerFacingSystem : public CCL::ECS::IfSystem<
    PlayerFacingSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<CharacterMovementComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Read<PlayerRushComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerFacingSystem() : IfSystem("PlayerFacingSystem") {}
    virtual ~PlayerFacingSystem() = default;

    void Update(float dt) override;

private:
    // 向き（yaw）と傾き（roll）を1ステップ進める。攻撃中は敵の方、それ以外は移動の向きへ回す
    void UpdateYawAndRoll(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player);

    // 旋回時の傾きの目標（rad）を返す。曲がる角度と、walkSpeed に対する今の速さの比で決める
    float CalcBankingRoll(float angleDiff, const CharacterMovementComponent &movement, float walkSpeed, const PlayerComponent &player) const;

    // 傾きを含む向きを Transform に、yaw だけを Jolt に書く
    void ApplyRotation(const PlayerComponent &player, TransformComponent &trans, const JoltCharacterHandleComponent &joltHandle);
};
