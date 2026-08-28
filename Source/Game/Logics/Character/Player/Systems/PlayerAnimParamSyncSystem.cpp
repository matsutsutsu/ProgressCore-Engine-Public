/// @file PlayerAnimParamSyncSystem.cpp

#include "PlayerAnimParamSyncSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// 毎フレームの HashString 計算を省くための事前計算（キャッシュ）
static const uint32_t HASH_IS_DASHING         = CCL::Utils::HashString("IsDashing");
static const uint32_t HASH_IS_AIRBORNE        = CCL::Utils::HashString("IsAirborne");
static const uint32_t HASH_MOVE_INPUT         = CCL::Utils::HashString("MoveInput");
static const uint32_t HASH_DASH_ATTACK_READY  = CCL::Utils::HashString("DashAttackReady");

void PlayerAnimParamSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const DashComponent &dash, const PlayerComponent &player,
                const CharacterMovementComponent &movement, AnimParametersComponent &animParams) {
        animParams.SetBool(HASH_IS_DASHING, dash.isDashing);

        // グレース付きの空中判定を掲示板へ。アニメグラフはこれを条件に使うことで、
        // YVelocity/IsGrounded 直読みのように小さな段差で落下へ遷移しなくなる。
        animParams.SetBool(HASH_IS_AIRBORNE, player.isAirborne);

        // プレイヤーの「移動しようとしている意思」を掲示板へ。攻撃はルートモーション駆動で
        // Speed(実速度) が入力を反映しないため、攻撃のキャンセル窓で「移動へ抜ける」判定には
        // 実速度でなく入力意思(desiredVelocity)を使う。
        const bool wantsMove = movement.desiredVelocity.LengthSquared() > 0.01f;
        animParams.SetBool(HASH_MOVE_INPUT, wantsMove);

        // 「この攻撃をダッシュ攻撃にすべきか」をコード側の実測で判定してフラグ化する。
        // 判定材料：ダッシュ中 / 走行中 / 慣性で十分な速度が残っている（走行解除直後の猶予）。
        //   → ノードエディタは Speed の数値比較をやめ、この bool と Attack トリガーだけで
        //      DashAttack / 通常 Attack_1 を分岐する（数値判定はコード、遷移はグラフに分離）。
        const float dashAtkSpeed   = dash.dashSpeed * 0.5f;
        const bool  dashAttackReady = dash.isDashing || player.isRunning
            || movement.currentVelocity.LengthSquared() >= (dashAtkSpeed * dashAtkSpeed);
        animParams.SetBool(HASH_DASH_ATTACK_READY, dashAttackReady);
    });
}

// 掲示板を消費する AnimControllerSystem(L02_Update=200) より必ず前に走らせる。
REGISTER_LOGIC_SYSTEM(PlayerAnimParamSyncSystem, Priority::LogicStage::L02_PreUpdate);
