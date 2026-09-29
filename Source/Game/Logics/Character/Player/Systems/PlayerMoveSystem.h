#pragma once
// ===========================================================================
// 概要: プレイヤーのジャンプと向き（旋回・傾き）を処理する System。
//
// 責務:
//   - 接地状態から isAirborne を猶予付きで決める
//   - 2段ジャンプを判定し、物理へジャンプの要求を渡してイベントとアニメのトリガーを出す
//   - 移動方向（攻撃時は敵の方向）へ向き直り、旋回時の傾きを付ける
//   - 傾きを含む回転を Transform に、yaw だけを Jolt の CharacterVirtual に書く
//
// 備考: 移動速度は書かない（PlayerDashSystem と CharacterMovementComponent の担当）。
// ===========================================================================

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<PlayerMoveSystem,
                                                   CCL::ECS::Write<TransformComponent>,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Read<PlayerInputComponent>,
                                                   CCL::ECS::Read<JoltCharacterConfigComponent>,
                                                   CCL::ECS::Read<JoltCharacterHandleComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Write<AnimParametersComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}
    virtual ~PlayerMoveSystem() = default;

    void Update(float dt) override;

private:
    // 接地状態を更新し、ジャンプできるかを判定する（2段ジャンプを含む）。
    // 押した瞬間の判定は PlayerInputComponent で済んでいるので、ここは
    // 跳んでよいかだけを判断し、結果を jumpImpulse として物理へ渡す。
    // 接地を離れたことは airborneGraceTime の猶予付きで isAirborne へ反映する。
    // 跳んだときは worldPos を載せて PlayerJumpedEvent を出す（VFX 用）。
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    // 向きと旋回時の傾きを計算し、傾きを含む回転を Transform に、yaw だけを Jolt に書く
    void UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character);
};