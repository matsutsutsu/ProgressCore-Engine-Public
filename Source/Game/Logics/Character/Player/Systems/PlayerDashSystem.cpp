#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // 画角要求(wideFovRequest)をカメラへ push する
#include "Engine/Animation/Components/RootMotionComponent.h" // ダッシュ中の外部移動制御を伝える
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Combat/HealthComponent.h" // 回避の無敵：被弾処理が見る i-frame タイマーへ反映する
#include "Game/Logics/GameEvent/GameEvents.h"   // ダッシュ開始を announce（その場VFX用）

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerDashSystem::Update(float rawDt)
{
    // 接近先(ボス)と世界スロー倍率をフレーム先頭で拾っておく（1対1なので探索不要）。
    const BattleContext *battle  = m_world->HasResource<BattleContext>() ? &m_world->GetResource<BattleContext>() : nullptr;
    const TimeContext   *timeCtx = m_world->HasResource<TimeContext>() ? &m_world->GetResource<TimeContext>() : nullptr;

    ForEachWithID([&](EntityID                        id,
                CharacterMovementComponent &movement,
                DashComponent                   &dash,
                PlayerComponent                 &player,
                const PlayerInputComponent      &pin,
                const CleaveComponent           &cleave,
                const TransformComponent        &trans,
                TimeStateComponent              &time) {
        // ルートモーション対応キャラなら、後段でダッシュ状態を「外部移動制御中」として伝える。
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);

        // 回避(ダッシュ/接近)中は、被弾処理が見る無敵タイマーを毎フレーム維持する。
        MaintainInvincibility(id, dash);

        // 接近を消化したフレームは、以降のダッシュ/移動処理を行わない。
        if (UpdateApproach(rawDt, battle, timeCtx, dash, movement, trans, time, rm))
            return;

        // 切り払いの準備/突進中は回避を開始させない。切り払いは L02_PostUpdate で
        // 速度を上書きするため、ここで回避が成立すると見た目は切り払いのまま
        // 回避のクールダウンと無敵だけを消費してしまう。
        const bool dashPressed = pin.dashPressed && (cleave.phase == CleavePhase::Idle);

        // ダッシュ（開始/継続/終了→走行）。ダッシュでなければ地上ロコモーション（走行/歩き）。
        if (!UpdateDash(dash, player, dashPressed, movement, trans, time, id))
            UpdateGroundLocomotion(player, movement, time.localDt);

        // ダッシュ中は「外部が移動を制御中」を root motion へ伝える（＝抽出を止めさせる）。
        if (rm) rm->externallyDriven = dash.isDashing;

        // 画角ワイド要求をカメラへ push（ダッシュ中のみ広げる）。
        PushWideFovRequest(id, dash.isDashing);
    });
}

// ---------------------------------------------------------------------------
// 回避(ダッシュ/接近)中、被弾処理が実際に参照する HealthComponent.invincibilityTimer を
// 毎フレーム invincibleWindow へ維持し、ダメージ/ドローン接触をすり抜けさせる。
// ヒットストップ中は localDt=0 でこのタイマーが減らないため凍結され、接近の「溜め」中も無敵が保たれる。
// ---------------------------------------------------------------------------
void PlayerDashSystem::MaintainInvincibility(EntityID id, const DashComponent &dash)
{
    if (!dash.isInvincible) return;
    if (auto *health = m_world->GetComponent<HealthComponent>(id))
    {
        if (health->invincibilityTimer < dash.invincibleWindow)
            health->invincibilityTimer = dash.invincibleWindow;
    }
}

// ---------------------------------------------------------------------------
// 接近（ジャスト回避スロー中にボスへ詰める「接近専用時間」）を処理する。
// 通常ダッシュより優先。世界はスローのまま、プレイヤーだけ実効時間で高速に寄る。
// 接近中だった場合は true を返し、呼び出し側はこのフレームの残り処理をスキップする。
// ---------------------------------------------------------------------------
bool PlayerDashSystem::UpdateApproach(float rawDt, const BattleContext *battle, const TimeContext *timeCtx,
                                      DashComponent &dash, CharacterMovementComponent &movement,
                                      const TransformComponent &trans, TimeStateComponent &time,
                                      RootMotionComponent *rm)
{
    if (!dash.isApproaching) return false;

    // 接近は非ダッシュ扱い（従来どおり root motion 抑制の対象外）。
    // 接近中はこの関数が true を返して以降の処理を止めるため、ダッシュ入力は自然に無視される。
    if (rm) rm->externallyDriven = false;

    // 接近先(ボス)が居なければ打ち切る。
    if (!battle || battle->bossEntity == InvalidEntityID)
    {
        EndApproach(dash, time);
        return true;
    }

    // ヒットストップ中（ジャスト回避直後の一瞬の停止＝溜め）は待機。
    // 明けてから接近を開始することで「停止→スリングショット」の手触りを作る。
    if (time.hitStopTimer > 0.0f)
        return true;

    // 時間切れ、または殴れる距離まで詰めたら終了。
    Vector3 toBoss(battle->bossPos.x - trans.position.x, 0.0f, battle->bossPos.z - trans.position.z);
    float   dist = toBoss.Length();

    dash.approachTimer -= rawDt;
    if (dash.approachTimer <= 0.0f || dist <= dash.approachStopRange)
    {
        EndApproach(dash, time);
        return true;
    }

    // ボス方向へ高速移動。接近中は無敵でスロー中の群れをすり抜ける。
    toBoss.Normalize();
    dash.dashDirection    = toBoss;
    dash.isInvincible     = true;
    movement.desiredVelocity = toBoss;
    movement.overrideSpeed   = dash.approachSpeed;

    // 世界スロー(globalScale × 勢力倍率)を打ち消し、プレイヤーだけ実効時間で動かす。
    if (timeCtx)
    {
        float worldScale = timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)];
        float safe       = (worldScale > 0.0001f) ? worldScale : 0.0001f;
        time.cinematicScale = dash.approachPlayerScale / safe;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ダッシュ（回避バースト）の開始・継続・終了を処理する。
// 終了時は歩きへ戻さず「走行(Run)」へ移行させる。
// このフレームをダッシュとして消費したら true（呼び出し側は地上ロコモーションを行わない）。
// ---------------------------------------------------------------------------
bool PlayerDashSystem::UpdateDash(DashComponent &dash, PlayerComponent &player, bool dashPressed,
                                  CharacterMovementComponent &movement, const TransformComponent &trans,
                                  const TimeStateComponent &time, CCL::ECS::EntityID id)
{
    // クールタイムの減算。
    if (!dash.isDashing && dash.currentTimer > 0.0f)
        dash.currentTimer -= time.localDt;

    // 開始判定。
    if (dashPressed && !dash.isDashing && dash.currentTimer <= 0.0f)
    {
        dash.isDashing               = true;
        dash.isInvincible            = true;
        dash.isJustEvadeWindowActive = true; // ダッシュ開始と同時にジャスト受付開始
        dash.currentTimer            = dash.dashDuration;
        player.locomotion            = PlayerComponent::GroundLocomotion::Dodge; // 回避バースト中

        // 移動入力があればその方向、無入力ならキャラの正面方向へダッシュ。
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

        // その場VFX用に「ダッシュ開始」を announce する（VFX/プレハブは購読側が知る）。
        m_world->GetEventBus().Publish(PlayerDashedEvent{trans.position,id});
    }

    if (!dash.isDashing) return false; // ダッシュしていない → 呼び出し側で歩き/走り

    // 継続：タイマーを進め、ジャスト受付窓の締め切りを判定する。
    dash.currentTimer -= time.localDt;

    // ダッシュ開始から指定時間(justEvadeWindow×チェイン倍率)が経過したら、ジャスト受付のみ終了する。
    // チェインが伸びるほど windowScale が小さくなり、受付が厳しくなる（リスク上昇）。
    float elapsed         = dash.dashDuration - dash.currentTimer;
    float effectiveWindow = dash.justEvadeWindow * dash.justEvadeWindowScale;
    if (elapsed > effectiveWindow)
        dash.isJustEvadeWindowActive = false;

    if (dash.currentTimer <= 0.0f)
    {
        // 終了（クールダウンへ移行）。受付窓は上の経過判定でも閉じるが、途中終了経路で
        // 閉じ損ねるため終了時に必ず落とす。
        dash.isDashing               = false;
        dash.isInvincible            = false;
        dash.isJustEvadeWindowActive = false;
        dash.currentTimer            = dash.dashCooldown;

        // ★ 回避バースト終了 → 歩きへは戻さず「走行(Run)」へ移行する。
        //   以降は地上ロコモーションが runSpeed を維持し、無入力が続いたら歩きへ自動解除する。
        //   ダッシュ攻撃はこの走行中ずっと出せる。
        player.isRunning    = true;
        player.runIdleTimer = 0.0f;

        // 終了フレームは走行として扱わせる（false を返して地上ロコモーションへ渡す）。
        //   上書き要求は1フレーム限りなので、ここで何も書かずに抜けると
        //   このフレームだけ歩き速度に落ちて、ダッシュ→走行の繋ぎに段差ができる。
        return false;
    }

    // 実行中：方向と速度を強制上書きする。
    movement.desiredVelocity = dash.dashDirection;
    movement.overrideSpeed   = dash.dashSpeed;
    return true;
}

// ---------------------------------------------------------------------------
// 非ダッシュ時の地上ロコモーション（走行 or 歩き）。
//   走行中は移動入力がある限り runSpeed を維持し、無入力が runIdleReleaseTime 続いたら
//   歩きへ自動解除する（トグル＋自動解除）。
// ---------------------------------------------------------------------------
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
                player.isRunning = false; // 無入力が続いた → 歩きへ
        }
    }

    if (player.isRunning)
    {
        movement.overrideSpeed = player.runSpeed; // 走行速度で維持
        player.locomotion   = PlayerComponent::GroundLocomotion::Run;
    }
    else
    {
        // 歩きは何も書かない。overrideSpeed は読まれた時点で既定へ戻るため、
        // 「書かない」ことが config.walkSpeed で歩くという表明になる。
        player.locomotion = PlayerComponent::GroundLocomotion::Walk;
    }
}

// ---------------------------------------------------------------------------
// このプレイヤーを映している TPS カメラへ「画角を広げてほしい」要求を push する。
// カメラ(Engine)が DashComponent(Game) を読みに来ないよう、状態の持ち主から伝える。
// ---------------------------------------------------------------------------
void PlayerDashSystem::PushWideFovRequest(EntityID playerID, bool wide)
{
    for (EntityID camID : m_world->View<CameraBodyTPSComponent>())
    {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) continue;
        tps->wideFovRequest = wide;
    }
}

// ---------------------------------------------------------------------------
// 接近を終了し、プレイヤーの時計を通常へ戻す共通処理。
// ---------------------------------------------------------------------------
void PlayerDashSystem::EndApproach(DashComponent &dash, TimeStateComponent &time)
{
    dash.isApproaching  = false;
    dash.isInvincible   = false;
    dash.approachTimer  = 0.0f;
    dash.currentTimer   = dash.dashCooldown; // 接近後はダッシュのクールダウンへ
    time.cinematicScale = 1.0f;              // 世界スロー打ち消しを解除（通常の時間へ復帰）
}

// MoveSystemで振り向かせる前(または同タイミング)に実行
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
