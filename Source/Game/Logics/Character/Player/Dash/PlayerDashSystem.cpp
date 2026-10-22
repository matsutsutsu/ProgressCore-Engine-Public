#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // カメラへ画角の要求(wideFovRequest)を書く
#include "Game/Logics/Combat/Components/HealthComponent.h"         // 回避の無敵を、被弾処理が見る無敵タイマーへ書く
#include "Game/Core/GameEvents.h"                                  // ダッシュ開始のイベント（VFX 用）

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerDashSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      PlayerDashComponent              &dash,
                      PlayerComponent            &player,
                      const PlayerInputComponent &playerInput,
                      const TransformComponent   &trans,
                      const TimeStateComponent   &time) {
        // 処理フロー:
        // 1. 回避中の無敵を維持する
        // 2. ダッシュを処理する（ダッシュでなければ、歩き/走りは後に走る PlayerMoveSystem が書く）
        // 3. カメラ画角の要求を書く

        // --- 無敵の維持 ---
        MaintainInvincibility(id, dash);

        // --- ダッシュ ---
        UpdateDash(dash, player, playerInput.dashPressed, movement, trans, time, id);

        // --- カメラ画角 ---
        // ダッシュ中だけ画角を広げるよう、カメラへ書く。
        PushWideFovRequest(id, dash.isDashing);
    });
}

void PlayerDashSystem::MaintainInvincibility(EntityID id, const PlayerDashComponent &dash)
{
    // ヒットストップ中は localDt=0 なので無敵タイマーは減らず、停止中も無敵が続く。
    if (!dash.isInvincible)
    {
        return;
    }

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
    // 2. 条件を満たせばダッシュを開始する
    // 3. ダッシュ中なら継続し、時間切れで終了して走行へ移す

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
        dash.isJustEvadeWindowActive = true; // 開始と同時にジャスト回避の受付を始める
        dash.currentTimer            = dash.dashDuration;
        player.locomotion            = PlayerComponent::GroundLocomotion::Dodge;

        // 進む方向を確定する：移動入力があればその向き、無ければキャラの正面。
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

        // ダッシュ開始をイベントで知らせる（どの VFX を出すかは購読側が決める）。
        m_world->GetEventBus().Publish(PlayerDashedEvent{trans.position, id});
    }

    // ダッシュしていなければ何もしない（歩き/走りは PlayerMoveSystem が書く）。
    if (!dash.isDashing)
    {
        return;
    }

    // ---------------------------------------------------------
    // 3. 継続と終了
    // ---------------------------------------------------------
    dash.currentTimer -= time.localDt;

    // 開始から justEvadeWindow 秒が過ぎたら、ジャスト回避の受付だけを閉じる。
    const float elapsed = dash.dashDuration - dash.currentTimer;
    if (elapsed > dash.justEvadeWindow)
    {
        dash.isJustEvadeWindowActive = false;
    }

    if (dash.currentTimer <= 0.0f)
    {
        // 終了してクールダウンに入る。経過時間の判定を通らずに終わる場合もあるので、受付はここでも必ず閉じる。
        dash.isDashing               = false;
        dash.isInvincible            = false;
        dash.isJustEvadeWindowActive = false;
        dash.currentTimer            = dash.dashCooldown;

        // ダッシュが終わったら、歩きには戻さず走行に移す。
        //   以降は PlayerMoveSystem が runSpeed を保ち、無入力が続いたら歩きに戻す。
        //   終了したステップの速さも、同じステップで後に走る PlayerMoveSystem が書く
        //   （上書き要求は1ステップ限りなので、誰も書かないとこのステップだけ歩き速度に落ちる）。
        player.isRunning    = true;
        player.runIdleTimer = 0.0f;
        return;
    }

    // ダッシュ中は方向と速度を上書きする。
    movement.desiredVelocity = dash.dashDirection;
    movement.overrideSpeed   = dash.dashSpeed;
}

void PlayerDashSystem::PushWideFovRequest(EntityID playerID, bool wide)
{
    // カメラ(Engine)が PlayerDashComponent(Game) を読まずに済むよう、状態を持つ側から書き込む。
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID)
        {
            return;
        }
        tps->wideFovRequest = wide;
    });
}

// PlayerInputSystem(L01_Input) が書いた移動の要求を読み、物理(L03以降)より前に速度を書く。
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
