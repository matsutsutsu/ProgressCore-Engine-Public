#pragma once
// ===========================================================================
// 概要: プレイヤーのジャンプと向き（旋回・傾き）を処理する System。
//
// 責務:
//   - 接地の状態から、猶予付きで isAirborne を決める
//   - 2段ジャンプを判定し、物理へジャンプを依頼する
//   - 移動の向き（攻撃時は敵の向き）へ向き直り、旋回時の傾きを付ける
//   - 傾きを含む向きを Transform に、yaw だけを Jolt に書く
//
// 備考: 移動の速さは書かない（歩き・走りは PlayerMoveSystem、ダッシュは PlayerDashSystem）。
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

class PlayerJumpFacingSystem : public CCL::ECS::IfSystem<PlayerJumpFacingSystem,
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
    /**
     * @brief 接地の状態を更新し、跳べるなら物理へジャンプを依頼する（2段ジャンプを含む）。
     * @param dt          経過時間（秒）。
     * @param isGrounded  物理から見て接地しているか。
     * @param jumpPressed このステップにジャンプを押した瞬間か。
     * @param movement    jumpImpulse を書く先。
     * @param player      ジャンプの回数と isAirborne を持つプレイヤーの状態。
     * @param animParams  2段目のトリガーを引く先。
     * @param worldPos    プレイヤーの位置（ワールド）。PlayerJumpedEvent に載せる。
     */
    void UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos);

    /**
     * @brief 向きと旋回時の傾きを1ステップ分進め、Transform と Jolt に書く。
     * @param dt         経過時間（秒）。
     * @param movement   移動の向きと今の速さ。
     * @param walkSpeed  歩く速さ（m/秒）。傾きの大きさを、これに対する速さの比で決める。
     * @param player     向き・傾きとその調整値を持つプレイヤーの状態。
     * @param trans      傾きを含む向きを書く先。
     * @param joltHandle yaw だけを書く先。
     */
    void UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, const JoltCharacterHandleComponent &joltHandle);
};
