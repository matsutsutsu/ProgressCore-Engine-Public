#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // カメラへ画角の要求(wideFovRequest)を書く
#include "Engine/Animation/Components/RootMotionComponent.h"       // ダッシュ中はルートモーションの移動を止める
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Combat/Components/HealthComponent.h" // 回避の無敵を、被弾処理が見る無敵タイマーへ書く
#include "Game/Core/GameEvents.h"   // ダッシュ開始のイベント（VFX 用）

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerDashSystem::Update(float rawDt)
{
    // 接近先のボスと世界スローの倍率を先に取っておく（BattleContext がボスを1体だけ持つので探さなくてよい）。
    const BattleContext *battle  = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    const TimeContext   *timeCtx = m_world->HasResource<TimeContext>() ? &m_world->GetResource<TimeContext>() : nullptr;

    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      DashComponent              &dash,
                      PlayerComponent            &player,
                      const PlayerInputComponent &pin,
                      const CleaveComponent      &cleave,
                      const TransformComponent   &trans,
                      TimeStateComponent         &time) {
        // ルートモーションを持つキャラなら、後でダッシュ中かを externallyDriven に書く。
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);

        MaintainInvincibility(id, dash);

        // 接近中のフレームは、以降のダッシュ・移動の処理を行わない。
        if (UpdateApproach(rawDt, battle, timeCtx, dash, movement, trans, time, rm))
            return;

        // 切り払いの準備・突進中はダッシュを始めさせない。切り払いは L02_PostUpdate で
        // 速度を上書きするため、ここでダッシュを始めると、見た目は切り払いのまま
        // ダッシュのクールダウンと無敵だけを使ってしまう。
        const bool dashPressed = pin.dashPressed && (cleave.phase == CleavePhase::Idle);

        if (!UpdateDash(dash, player, dashPressed, movement, trans, time, id))
            UpdateGroundLocomotion(player, movement, time.localDt);

        // ダッシュ中は、ルートモーションによる移動を止めさせる。
        if (rm) rm->externallyDriven = dash.isDashing;

        // ダッシュ中だけ画角を広げるよう、カメラへ書く。
        PushWideFovRequest(id, dash.isDashing);
    });
}

void PlayerDashSystem::MaintainInvincibility(EntityID id, const DashComponent &dash)
{
    // ヒットストップ中は localDt=0 なので無敵タイマーは減らず、接近を始める前の停止中も無敵が続く。

    if (!dash.isInvincible) return;
    if (auto *health = m_world->GetComponent<HealthComponent>(id))
    {
        if (health->invincibilityTimer < dash.invincibleWindow)
            health->invincibilityTimer = dash.invincibleWindow;
    }
}

// ---------------------------------------------------------------------------
// 段階ごとの更新
// ---------------------------------------------------------------------------

bool PlayerDashSystem::UpdateApproach(float rawDt, const BattleContext *battle, const TimeContext *timeCtx,
                                      DashComponent &dash, CharacterMovementComponent &movement,
                                      const TransformComponent &trans, TimeStateComponent &time,
                                      RootMotionComponent *rm)
{
    if (!dash.isApproaching) return false;

    // 接近はダッシュとして扱わず、ルートモーションを止めない。
    // 接近中はこの関数が true を返して以降の処理を止めるので、ダッシュの入力は無視される。
    if (rm) rm->externallyDriven = false;

    // 接近先(ボス)が居なければ打ち切る。
    if (!battle || battle->bossEntity == InvalidEntityID)
    {
        EndApproach(dash, time);
        return true;
    }

    // ヒットストップ中（ジャスト回避直後に一瞬止まっている間）は待つ。
    // 止まった後に一気に飛び出す動きにするため、明けてから接近を始める。
    if (time.hitStopTimer > 0.0f)
        return true;

    // 時間切れか、攻撃が届く距離まで近づいたら終える。
    Vector3 toBoss(battle->bossPos.x - trans.position.x, 0.0f, battle->bossPos.z - trans.position.z);
    float   dist = toBoss.Length();

    dash.approachTimer -= rawDt;
    if (dash.approachTimer <= 0.0f || dist <= dash.approachStopRange)
    {
        EndApproach(dash, time);
        return true;
    }

    // ボスの方向へ高速で移動する。接近中は無敵なので、スロー中のドローンに当たらない。
    toBoss.Normalize();
    dash.dashDirection       = toBoss;
    dash.isInvincible        = true;
    movement.desiredVelocity = toBoss;
    movement.overrideSpeed   = dash.approachSpeed;

    // 世界スロー（globalScale × グループ倍率）を打ち消し、プレイヤーだけ approachPlayerScale の速さで動かす。
    if (timeCtx)
    {
        float worldScale    = timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)];
        float safe          = (worldScale > 0.0001f) ? worldScale : 0.0001f;
        time.cinematicScale = dash.approachPlayerScale / safe;
    }
    return true;
}

bool PlayerDashSystem::UpdateDash(DashComponent &dash, PlayerComponent &player, bool dashPressed,
                                  CharacterMovementComponent &movement, const TransformComponent &trans,
                                  const TimeStateComponent &time, CCL::ECS::EntityID id)
{
    // クールダウン
    if (!dash.isDashing && dash.currentTimer > 0.0f)
        dash.currentTimer -= time.localDt;

    // 開始
    if (dashPressed && !dash.isDashing && dash.currentTimer <= 0.0f)
    {
        dash.isDashing               = true;
        dash.isInvincible            = true;
        dash.isJustEvadeWindowActive = true; // ダッシュ開始と同時にジャスト回避の受付を始める
        dash.currentTimer            = dash.dashDuration;
        player.locomotion            = PlayerComponent::GroundLocomotion::Dodge;

        // 移動入力があればその方向、無ければキャラの正面へダッシュする。
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

    if (!dash.isDashing) return false; // ダッシュしていなければ、呼び出し側で歩き/走りを処理する

    // 継続
    dash.currentTimer -= time.localDt;

    // ダッシュ開始から justEvadeWindow 秒が過ぎたら、ジャスト回避の受付だけを閉じる。
    float elapsed = dash.dashDuration - dash.currentTimer;
    if (elapsed > dash.justEvadeWindow)
        dash.isJustEvadeWindowActive = false;

    if (dash.currentTimer <= 0.0f)
    {
        // 終了してクールダウンに入る。受付は上の経過時間の判定でも閉じるが、
        // それを通らずに終わる場合もあるので、終了時にも必ず閉じる。
        dash.isDashing               = false;
        dash.isInvincible            = false;
        dash.isJustEvadeWindowActive = false;
        dash.currentTimer            = dash.dashCooldown;

        // ダッシュが終わったら、歩きには戻さず走行に移る。
        //   以降は UpdateGroundLocomotion が runSpeed を保ち、無入力が続いたら歩きに戻す。
        //   ダッシュ攻撃は走行中ずっと出せる。
        player.isRunning    = true;
        player.runIdleTimer = 0.0f;

        // 終了フレームは走行として扱う（false を返して UpdateGroundLocomotion に任せる）。
        //   上書きの要求は1フレーム限りなので、ここで何も書かずに終えると、
        //   このフレームだけ歩きの速度に落ち、ダッシュから走行への切り替えで速度が一瞬落ちる。
        return false;
    }

    // ダッシュ中は方向と速度を上書きする。
    movement.desiredVelocity = dash.dashDirection;
    movement.overrideSpeed   = dash.dashSpeed;
    return true;
}

void PlayerDashSystem::UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt)
{
    if (player.isRunning)
    {
        const bool hasMoveInput = movement.desiredVelocity.LengthSquared() > 0.001f;
        if (hasMoveInput)
        {
            player.runIdleTimer = 0.0f;
        }
        else
        {
            player.runIdleTimer += dt;
            if (player.runIdleTimer >= player.runIdleReleaseTime)
                player.isRunning = false; // 無入力が続いたので歩きに戻す
        }
    }

    if (player.isRunning)
    {
        movement.overrideSpeed = player.runSpeed;
        player.locomotion      = PlayerComponent::GroundLocomotion::Run;
    }
    else
    {
        // 歩きのときは overrideSpeed を書かない。overrideSpeed は読まれた時点で既定へ戻るので、
        // 書かなければ config.walkSpeed で歩く。
        player.locomotion = PlayerComponent::GroundLocomotion::Walk;
    }
}

// ---------------------------------------------------------------------------
// 補助
// ---------------------------------------------------------------------------

void PlayerDashSystem::PushWideFovRequest(EntityID playerID, bool wide)
{
    // カメラ(Engine)が DashComponent(Game) を読まずに済むよう、状態を持つ側から書き込む。

    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) return;
        tps->wideFovRequest = wide;
    });
}

void PlayerDashSystem::EndApproach(DashComponent &dash, TimeStateComponent &time)
{
    dash.isApproaching  = false;
    dash.isInvincible   = false;
    dash.approachTimer  = 0.0f;
    dash.currentTimer   = dash.dashCooldown; // 接近の後はダッシュのクールダウンに入る
    time.cinematicScale = 1.0f;              // 世界スローの打ち消しをやめる
}

// PlayerInputSystem(L01_Input) が書いた移動の要求を読み、PlayerCleaveSystem(L02_PostUpdate) より前に速度を書く。
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
