#include "PlayerAnimParamSyncSystem.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

namespace
{
    // アニメのパラメータ名のハッシュ（毎ステップ計算しないよう先に求める）
    const uint32_t HASH_IS_DASHING        = CCL::Utils::HashString("IsDashing");
    const uint32_t HASH_IS_AIRBORNE       = CCL::Utils::HashString("IsAirborne");
    const uint32_t HASH_MOVE_INPUT        = CCL::Utils::HashString("HasMoveInput");
    const uint32_t HASH_IS_RUNNING        = CCL::Utils::HashString("IsRunning");
    const uint32_t HASH_DASH_ATTACK_READY = CCL::Utils::HashString("CanDashAttack");
    const uint32_t HASH_TRIG_DASH         = CCL::Utils::HashString("Dash");

    constexpr float kMinMoveInputSq       = 0.01f; // 移動の入力があると判定する長さの2乗
    constexpr float kDashAttackSpeedRatio = 0.5f;  // ダッシュの速さに対して、ダッシュ攻撃を出せる速さの割合
}



void PlayerAnimParamSyncSystem::Update(float dt)
{
    ForEach([&](const PlayerDashComponent &dash, const PlayerComponent &player,
                const CharacterMovementComponent &movement, AnimParametersComponent &animParams) {

        // ダッシュに入るのは、始めた瞬間の Dash トリガーで行う
        // IsDashing で入ると、ダッシュ中に移った先（DashAttack）を毎ステップ Dash へ引き戻す
        const bool wasDashing = animParams.GetBool(HASH_IS_DASHING);
        if (dash.isDashing && !wasDashing)
        {
            animParams.SetTrigger(HASH_TRIG_DASH);
        }
        animParams.SetBool(HASH_IS_DASHING, dash.isDashing);

        // 空中にいるかどうかはisAirborneを見る
        // 小さな段差で反応しないようにGroundで判定はしない
        animParams.SetBool(HASH_IS_AIRBORNE, player.isAirborne);

        // 移動の入力は入力を取得する
        const bool wantsMove = movement.desiredVelocity.LengthSquared() > kMinMoveInputSq;
        animParams.SetBool(HASH_MOVE_INPUT, wantsMove);

        // 走っている状態をセットする
        animParams.SetBool(HASH_IS_RUNNING, player.isRunning);

        // ダッシュ攻撃ができるかどうか
        // 速さは水平だけで測る。空中ダイブの後は落ちる速さも入っているので、それを数えないようにする
        const DirectX::SimpleMath::Vector3 horizontalVel(movement.currentVelocity.x, 0.0f, movement.currentVelocity.z);

        const float dashAtkSpeed    = dash.dashSpeed * kDashAttackSpeedRatio;
        const bool  stillFast       = horizontalVel.LengthSquared() >= (dashAtkSpeed * dashAtkSpeed);
        const bool  dashAttackReady = dash.isDashing || player.isRunning || stillFast;
        animParams.SetBool(HASH_DASH_ATTACK_READY, dashAttackReady);
    });
}



// パラメータを読む AnimControllerSystem(L02_Update) より前に書く
REGISTER_LOGIC_SYSTEM(PlayerAnimParamSyncSystem, Priority::LogicStage::L02_PreUpdate);
