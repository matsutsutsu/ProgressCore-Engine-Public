#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Game/Logics/Camera/PlayerCameraComponent.h"
#include "Game/Logics/Camera/PlayerCameraLookup.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"
#include "Game/Core/GameEvents.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    const uint32_t kCameraSource   = CCL::Utils::HashString("DashCamera"); // 構図の上書きスロットの発生源
    constexpr int  kCameraPriority = 0;                                    // 他の演出と重なったときの順（大きいほど後）。ラッシュは 10
}

std::vector<TypeID> PlayerDashSystem::GetWriteTypes() const
{
    auto types = Base::GetWriteTypes();
    types.push_back(TypeInfo<PlayerCameraComponent>::ID()); // カメラの構図の上書きスロット
    return types;
}



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

        PushCameraFraming(id, dash);
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



// ダッシュ中は画角の足し込みをカメラの上書きスロットへ書き、終わったら手放す
void PlayerDashSystem::PushCameraFraming(EntityID playerID, const PlayerDashComponent &dash)
{
    // 状態を持つゲーム側から書く。カメラはダッシュを知らず、足し込み量だけを受け取る
    // 画角は weight を 0/1 で切り替えるだけでいい。カメラ側が fovDamping でなめらかに寄せる
    auto *tps = m_world->GetComponent<PlayerCameraComponent>(FindPlayerCamera(*m_world, playerID));
    if (!tps) return;

    if (!dash.isDashing)
    {
        tps->framingOverrides.Release(kCameraSource);
        return;
    }

    if (auto *slot = tps->framingOverrides.Acquire(kCameraSource, kCameraPriority))
    {
        slot->weight           = 1.0f;
        slot->params           = CameraFramingParams{};
        slot->params.fovOffset = dash.cameraFovOffset;
    }
}



// 入力（L01_Input）の後、物理（L03）より前に速さを書く
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
