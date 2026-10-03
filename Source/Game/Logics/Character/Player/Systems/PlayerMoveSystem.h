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
    /**
     * @brief 接地状態を更新し、ジャンプできるかを判定する（2段ジャンプを含む）。
     *
     * 押した瞬間の判定は PlayerInputComponent で済んでいるので、ここは跳んでよいかだけを判断し、結果を jumpImpulse として物理へ渡す。
     *
     * 接地を離れたことは airborneGraceTime の猶予付きで isAirborne へ反映する。
     *
     * @param dt          経過時間（秒）。
     * @param isGrounded  物理から見て接地しているか。
     * @param jumpPressed このフレームにジャンプの入力があったか。
     * @param movement    jumpImpulse を書く先。
     * @param player      ジャンプ回数と isAirborne を持つプレイヤーの状態。
     * @param animParams  ジャンプのアニメのパラメータを書く先。
     * @param worldPos    プレイヤーの位置（ワールド）。跳んだときに PlayerJumpedEvent へ載せる（VFX 用）。
     */
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    /**
     * @brief 向きと旋回時の傾き（バンク）を計算し、傾きを含む回転を Transform に、yaw だけを Jolt に書く。
     * @param dt        経過時間（秒）。
     * @param movement  今の移動速度。傾きの大きさに使う。
     * @param walkSpeed 歩きの速さ（m/秒）。傾きは「曲がる角度 × 速さ」に比例させ、速さはこれに対する比で測る。
     * @param player    傾きの調整値（bankingMultiplier・maxBankingAngle）と今の向きを持つプレイヤーの状態。
     * @param trans     傾きを含む回転を書く Transform。
     * @param character yaw だけを書く Jolt のキャラクター。所有しない。
     */
    void UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character);
};