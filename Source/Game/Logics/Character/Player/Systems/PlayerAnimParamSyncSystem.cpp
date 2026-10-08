#include "PlayerAnimParamSyncSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// パラメータ名のハッシュ。毎フレーム HashString を計算しないよう先に求めておく
static const uint32_t HASH_IS_DASHING        = CCL::Utils::HashString("IsDashing");
static const uint32_t HASH_IS_AIRBORNE       = CCL::Utils::HashString("IsAirborne");
static const uint32_t HASH_MOVE_INPUT        = CCL::Utils::HashString("MoveInput");
static const uint32_t HASH_DASH_ATTACK_READY = CCL::Utils::HashString("DashAttackReady");

void PlayerAnimParamSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const DashComponent &dash, const PlayerComponent &player,
                const CharacterMovementComponent &movement, AnimParametersComponent &animParams) {
        animParams.SetBool(HASH_IS_DASHING, dash.isDashing);

        // 猶予付きの空中判定を書く。アニメグラフがこれを条件に使えば、
        // YVelocity/IsGrounded を直接見る場合と違い、小さな段差で落下へ遷移しない。
        animParams.SetBool(HASH_IS_AIRBORNE, player.isAirborne);

        // 移動入力があるかを書く。攻撃中はルートモーションで動くので Speed(実速度) が入力を
        // 反映しない。そのため、攻撃のキャンセル区間で移動へ抜けるかの判定には、
        // 実速度ではなく入力(desiredVelocity)を使う。
        const bool wantsMove = movement.desiredVelocity.LengthSquared() > 0.01f;
        animParams.SetBool(HASH_MOVE_INPUT, wantsMove);

        // 次の攻撃をダッシュ攻撃にするかをコードで判定して bool にする。
        // 条件: ダッシュ中 / 走行中 / 十分な速度が残っている（走行が解除された直後の猶予）。
        //   アニメグラフはこの bool と Attack トリガーで DashAttack と Attack_1 を分ける
        //   （数値の判定はコード、遷移はグラフに分ける）。
        const float dashAtkSpeed    = dash.dashSpeed * 0.5f;
        const bool  dashAttackReady = dash.isDashing || player.isRunning || movement.currentVelocity.LengthSquared() >= (dashAtkSpeed * dashAtkSpeed);
        animParams.SetBool(HASH_DASH_ATTACK_READY, dashAttackReady);
    });
}

// AnimParameters を読む AnimControllerSystem(L02_Update=200) より前に走らせる。
REGISTER_LOGIC_SYSTEM(PlayerAnimParamSyncSystem, Priority::LogicStage::L02_PreUpdate);
