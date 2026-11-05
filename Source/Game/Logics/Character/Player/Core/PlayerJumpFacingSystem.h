#pragma once
// ===========================================================================
// 概要: プレイヤーのジャンプと向き（旋回・傾き）を処理する System
//
// 責務:
//   - 接地の状態から、猶予付きで isAirborne を決める
//   - 2段ジャンプを判定し、物理へジャンプを依頼する
//   - 移動の向き（攻撃時は敵の向き）へ向き直り、旋回時の傾きを付ける
//   - 傾きを含む向きを Transform に、yaw だけを Jolt に書く
//
// 備考: 移動の速さは書かない（歩き・走りは PlayerMoveSystem、ダッシュは PlayerDashSystem）
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerInputComponent.h"
#include "Game/Logics/Character/Player/Rush/PlayerRushComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class PlayerJumpFacingSystem : public CCL::ECS::IfSystem<
    PlayerJumpFacingSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<PlayerInputComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Write<AnimParametersComponent>,
    CCL::ECS::Read<PlayerRushComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerJumpFacingSystem() : IfSystem("PlayerJumpFacingSystem") {}
    virtual ~PlayerJumpFacingSystem() = default;

    void Update(float dt) override;

private:
    // 接地の状態を更新し、跳べるなら物理へジャンプを依頼する（2段ジャンプを含む）
    // worldPos は PlayerJumpedEvent に載せるプレイヤーの位置
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    // 向きと旋回時の傾きを1ステップ進め、傾きを含む向きを Transform に、yaw だけを Jolt に書く
    // 傾きの大きさは、walkSpeed に対する今の速さの比で決める
    void UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, const JoltCharacterHandleComponent &joltHandle);
};
