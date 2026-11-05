#include "PlayerAnimParamSyncSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

// アニメのパラメータ名のハッシュ（毎ステップ計算しないよう先に求める）
static const uint32_t HASH_IS_DASHING        = CCL::Utils::HashString("IsDashing");
static const uint32_t HASH_IS_AIRBORNE       = CCL::Utils::HashString("IsAirborne");
static const uint32_t HASH_MOVE_INPUT        = CCL::Utils::HashString("MoveInput");
static const uint32_t HASH_IS_RUNNING        = CCL::Utils::HashString("IsRunning");
static const uint32_t HASH_DASH_ATTACK_READY = CCL::Utils::HashString("DashAttackReady");
static const uint32_t HASH_TRIG_DASH         = CCL::Utils::HashString("OnDash");



void PlayerAnimParamSyncSystem::Update(float /*dt*/)
{
    ForEach([&](const PlayerDashComponent &dash, const PlayerComponent &player,
                const CharacterMovementComponent &movement, AnimParametersComponent &animParams) {
        // ダッシュに入るのは、始めた瞬間の OnDash トリガーで行う
        // IsDashing で入ると、ダッシュ中に移った先（DashAttack）を毎ステップ Dash へ引き戻す
        const bool wasDashing = animParams.GetBool(HASH_IS_DASHING);
        if (dash.isDashing && !wasDashing)
        {
            animParams.SetTrigger(HASH_TRIG_DASH);
        }
        animParams.SetBool(HASH_IS_DASHING, dash.isDashing);

        // 空中かは、猶予付きの isAirborne を書く
        // YVelocity / IsGrounded を直接見ると、小さな段差で落下へ遷移する
        animParams.SetBool(HASH_IS_AIRBORNE, player.isAirborne);

        // 移動の入力があるかは、実際の速さでなく入力の向きで決める
        // 攻撃中はルートモーションで動くので、速さが入力を表さない
        const bool wantsMove = movement.desiredVelocity.LengthSquared() > 0.01f;
        animParams.SetBool(HASH_MOVE_INPUT, wantsMove);

        // 歩きか走りかは、速さでなくゲーム側の isRunning で決める
        // 止まりモーションを歩き用・走り用で分けるのに使う
        // 走りは無入力が続いてから解くので、手を離した瞬間はまだ走りのまま残っている
        animParams.SetBool(HASH_IS_RUNNING, player.isRunning);

        // 次の攻撃をダッシュ攻撃にするかを、コードで判定する
        // グラフはこの bool と Attack トリガーで、DashAttack と Attack_1 を分ける

        // ダッシュ中・走り中に加え、ダッシュの半分の速さが残っている間も出す
        // 走りを解いた直後の猶予にする
        const float dashAtkSpeed    = dash.dashSpeed * 0.5f;
        const bool  stillFast       = movement.currentVelocity.LengthSquared() >= (dashAtkSpeed * dashAtkSpeed);
        const bool  dashAttackReady = dash.isDashing || player.isRunning || stillFast;
        animParams.SetBool(HASH_DASH_ATTACK_READY, dashAttackReady);
    });
}



// パラメータを読む AnimControllerSystem(L02_Update) より前に書く
REGISTER_LOGIC_SYSTEM(PlayerAnimParamSyncSystem, Priority::LogicStage::L02_PreUpdate);
