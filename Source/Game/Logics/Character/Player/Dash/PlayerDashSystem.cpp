#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Game/Logics/Camera/CameraBodyActionTPSComponent.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Core/GameEvents.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerDashSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      PlayerDashComponent        &dash,
                      PlayerComponent            &player,
                      const PlayerInputComponent &playerInput,
                      const TransformComponent   &trans,
                      const TimeStateComponent   &time,
                      const PlayerRushComponent  &rush) {
        MaintainInvincibility(id, dash);

        // ラッシュ中は押しても始めない
        const bool dashPressed = playerInput.dashPressed && !rush.isActive;

        // ダッシュしていないときの歩き・走りは、後に走る PlayerMoveSystem が書く
        UpdateDash(dash, player, dashPressed, movement, trans, time, id);

        PushWideFovRequest(id, dash.isDashing);
    });
}



void PlayerDashSystem::MaintainInvincibility(EntityID id, const PlayerDashComponent &dash)
{
    if (!dash.isInvincible) return;

    // ヒットストップ中は localDt が 0 でタイマーが減らないので、止まっている間も無敵が続く
    if (auto *health = m_world->GetComponent<HealthComponent>(id))
    {
        if (health->invincibilityTimer < dash.invincibleWindow)
        {
            health->invincibilityTimer = dash.invincibleWindow;
        }
    }
}



void PlayerDashSystem::UpdateDash(PlayerDashComponent &dash, PlayerComponent &player, bool dashPressed,
                                  CharacterMovementComponent &movement, const TransformComponent &trans,
                                  const TimeStateComponent &time, CCL::ECS::EntityID id)
{
    // 処理フロー:
    // 1. クールダウンを進める
    // 2. 押されていて出せるなら、ダッシュを始める
    // 3. ダッシュ中なら向きと速さを書き、時間切れで走りへ移す

    // ---------------------------------------------------------
    // 1. クールダウン
    // ---------------------------------------------------------

    if (!dash.isDashing && dash.currentTimer > 0.0f)
    {
        dash.currentTimer -= time.localDt;
    }

    // ---------------------------------------------------------
    // 2. 開始
    // ---------------------------------------------------------

    if (dashPressed && !dash.isDashing && dash.currentTimer <= 0.0f)
    {
        dash.isDashing               = true;
        dash.isInvincible            = true;
        dash.isJustEvadeWindowActive = true;
        dash.currentTimer            = dash.dashDuration;

        // 進む向きを決める。移動の入力があればその向き、なければ正面
        if (movement.desiredVelocity.LengthSquared() > 0.001f)
        {
            dash.dashDirection = movement.desiredVelocity;
        }
        else
        {
            Vector3 forward    = Vector3::Transform(Vector3::UnitZ, trans.rotation);
            forward.y          = 0.0f;
            dash.dashDirection = (forward.LengthSquared() > 0.001f) ? forward : Vector3::UnitZ;
        }
        dash.dashDirection.Normalize();

        // 始めたことを知らせる（どの VFX を出すかは購読側が決める）
        m_world->GetEventBus().Publish(PlayerDashedEvent{trans.position, id});
    }

    if (!dash.isDashing) return;

    // ---------------------------------------------------------
    // 3. 継続と終了
    // ---------------------------------------------------------

    dash.currentTimer -= time.localDt;

    // 開始から justEvadeWindow を過ぎたら、ジャスト回避の受付だけを閉じる
    const float elapsed = dash.dashDuration - dash.currentTimer;
    if (elapsed > dash.justEvadeWindow) dash.isJustEvadeWindowActive = false;

    if (dash.currentTimer <= 0.0f)
    {
        // 終わったので、無敵と受付を閉じてクールダウンに入る
        // 受付は上の判定を通らずに終わることもあるので、ここでも閉じる
        dash.isDashing               = false;
        dash.isInvincible            = false;
        dash.isJustEvadeWindowActive = false;
        dash.currentTimer            = dash.dashCooldown;

        // 歩きに戻さず走りに移す（以後は PlayerMoveSystem が走りの速さを書く）
        player.isRunning    = true;
        player.runIdleTimer = 0.0f;
        return;
    }

    // ダッシュ中は、向きと速さを上書きする
    movement.desiredVelocity = dash.dashDirection;
    movement.overrideSpeed   = dash.dashSpeed;
}



void PlayerDashSystem::PushWideFovRequest(EntityID playerID, bool wide)
{
    // 状態を持つゲーム側から書く
    // カメラ（Engine）が PlayerDashComponent（Game）を読まずに済む
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) return;

        tps->wideFovRequest = wide;
    });
}



// 入力（L01_Input）の後、物理（L03）より前に速さを書く
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
