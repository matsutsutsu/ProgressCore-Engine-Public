#include "PlayerDashSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h" // カメラへ画角の要求(wideFovRequest)を書く
#include "Engine/Animation/Components/RootMotionComponent.h"       // ダッシュ中はルートモーションの移動を止める
#include "Engine/Time/TimeService.h"                               // ラッシュ中の世界スロー(SustainWorldSlow)・固定フリーズ(HitStop)
#include "Game/Logics/Combat/Components/HealthComponent.h" // 回避の無敵を、被弾処理が見る無敵タイマーへ書く
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h" // ラッシュのビネット(active)を切り替える
#include "Game/Core/GameEvents.h"   // ダッシュ開始のイベント（VFX 用）

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void PlayerDashSystem::Update(float rawDt)
{
    // 世界スローの倍率を先に取っておく（ラッシュ中にプレイヤーだけ実時間へ戻すのに使う）。
    const TimeContext *timeCtx = m_world->HasResource<TimeContext>() ? &m_world->GetResource<TimeContext>() : nullptr;

    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      DashComponent              &dash,
                      PlayerComponent            &player,
                      const PlayerInputComponent &pin,
                      const TransformComponent   &trans,
                      TimeStateComponent         &time) {
        // ルートモーションを持つキャラなら、後でダッシュ中かを externallyDriven に書く。
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);

        MaintainInvincibility(id, dash);

        // ラッシュ踏み込み(フェーズA)中のフレームは操作ロック＝以降のダッシュ・移動処理を飛ばす。
        //   集中スロー(フェーズB)は false が返るので、通常のダッシュ/歩行/攻撃がそのまま動く。
        if (UpdateFlurry(rawDt, timeCtx, dash, movement, time, rm))
            return;

        const bool dashPressed = pin.dashPressed;

        if (!UpdateDash(dash, player, dashPressed, movement, trans, time, id))
            UpdateGroundLocomotion(player, movement, time.localDt);

        // ダッシュ中は、ルートモーションによる移動を止めさせる。
        if (rm) rm->externallyDriven = dash.isDashing;

        // ダッシュ中だけ画角を広げるよう、カメラへ書く。
        PushWideFovRequest(id, dash.isDashing);

        // ラッシュを抜けた最初のフレームで、ビネットを一度だけ降ろす（＝フェードアウト開始）。
        //   フェードアウトは fadeOutTime 秒。これを世界スロー復帰(flurrySlowRecover)と揃えると、
        //   暗がりの消え方とスローの戻りがちょうど一致する。毎フレーム false を書かないのは、
        //   切り払い側が立てたビネットを横から消さないため（持ち主のときだけ降ろす）。
        const bool inFlurry = dash.isFlurryLunging || dash.isFlurryPlanting || dash.isFlurryFocusing;
        if (!inFlurry && m_flurryVignetteOwned)
        {
            SetFlurryVignette(false);
            m_flurryVignetteOwned = false;
        }
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

bool PlayerDashSystem::UpdateFlurry(float rawDt, const TimeContext *timeCtx,
                                    DashComponent &dash, CharacterMovementComponent &movement,
                                    TimeStateComponent &time, RootMotionComponent *rm)
{
    if (!dash.isFlurryLunging && !dash.isFlurryPlanting && !dash.isFlurryFocusing) return false;

    // 全フェーズ共通：世界をスローにしたまま、プレイヤーだけ実時間へ戻す（約分：global×group×cinematic＝playerScale）。
    //   worldScale は前フレームの SustainWorldSlow 要求が TimeScaleSystem(L01) で反映された結果。
    const float worldScale = timeCtx
        ? (timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)])
        : 1.0f;
    // 除数の下限は flurryWorldScale（＝狙った世界スロー）にする。プラントの全体フリーズで
    //   worldScale が 0 近くまで落ちても cinematicScale が playerScale/flurryWorldScale に頭打ちになり、
    //   フリーズ解除フレームで localDt が巨大化してプレイヤーが高速回転・暴走するのを防ぐ。
    //   （フリーズ中は globalScale=0 なので localDt=0＝プレイヤーは正しく固定されたまま）
    const float safeWorld      = (worldScale > dash.flurryWorldScale) ? worldScale : dash.flurryWorldScale;
    const float playerClockMul = dash.flurryPlayerScale / safeWorld;

    Time::SustainWorldSlow(*m_world, dash.flurryWorldScale, dash.flurrySlowRecover);
    time.cinematicScale = playerClockMul;
    dash.isInvincible   = true; // ラッシュ中は被弾させない（反撃のご褒美窓）

    // ラッシュ中はビネット演出を立て続ける（フェードイン→ピーク維持は JustEvadeEffectSystem が担う）。
    SetFlurryVignette(true);
    m_flurryVignetteOwned = true;

    // ── フェーズA：踏み込み（操作ロック・ease-outで前進） ──
    if (dash.isFlurryLunging)
    {
        if (rm) rm->externallyDriven = true; // ルートモーションの移動を止める（踏み込みで駆動する）

        dash.flurryTimer -= rawDt; // スローの掛からない実時間で進める＝踏み込みはスローに引きずられない
        const float dur     = (dash.flurryLungeDuration > 0.0001f) ? dash.flurryLungeDuration : 0.0001f;
        const float t       = std::clamp((dur - dash.flurryTimer) / dur, 0.0f, 1.0f);

        // ease-out の“移動”＝速度は高→低の減少カーブ。(1-t)^2 で出だし速く・終わりかけ急減速。
        const float speedFactor  = (1.0f - t) * (1.0f - t);
        movement.desiredVelocity = dash.dashDirection; // 回避方向（ダッシュ開始時に確定済み）
        movement.overrideSpeed   = dash.flurryLungeSpeed * speedFactor;

        if (dash.flurryTimer <= 0.0f)
        {
            // 踏み込み終了 → “ピタッ”と固定して止まりを見せるプラント(B)へ。
            dash.isFlurryLunging  = false;
            dash.isFlurryPlanting = true;
            dash.flurryTimer      = dash.flurryPlantDuration;
            // プラントの間だけ、別レイヤーの全体フリーズで止まりを強く見せる（世界スロー合成とは独立）。
            Time::HitStop(*m_world, dash.flurryPlantDuration, dash.flurryPlantFreeze);
        }
        return true; // 操作ロック
    }

    // ── フェーズB：プラント（固定して止まりを見せる・操作ロック） ──
    if (dash.isFlurryPlanting)
    {
        if (rm) rm->externallyDriven = true;
        movement.desiredVelocity = Vector3::Zero; // 入力方向を無効化して固定
        movement.overrideSpeed   = 0.0f;          // 完全停止（フリーズ中はそもそも積分されない）

        dash.flurryTimer -= rawDt; // フリーズ中も rawDt は進む＝実時間で一定の“溜め”を見せる
        if (dash.flurryTimer <= 0.0f)
        {
            // プラント終了 → モノクロの自由行動窓(C)へ“切り替え”。
            dash.isFlurryPlanting = false;
            dash.isFlurryFocusing = true;
            dash.flurryTimer      = dash.flurryFocusDuration;
        }
        return true; // 操作ロック
    }

    // ── フェーズC：集中スロー（操作自由・速度上書きしない。ビネットは共通ブロックで継続中） ──
    if (rm) rm->externallyDriven = false; // 通常移動に戻す

    dash.flurryTimer -= rawDt;
    if (dash.flurryTimer <= 0.0f)
        EndFlurry(dash, time); // モノクロはここでは消さない。世界スローが戻り切るまで Update の追従オフが保つ。

    // false を返すことで、このフレームは通常のダッシュ/歩行/攻撃をそのまま動かす（自由行動）。
    return false;
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

void PlayerDashSystem::EndFlurry(DashComponent &dash, TimeStateComponent &time)
{
    dash.isFlurryLunging  = false;
    dash.isFlurryPlanting = false;
    dash.isFlurryFocusing = false;
    dash.isInvincible     = false;
    dash.flurryTimer      = 0.0f;
    dash.currentTimer     = dash.dashCooldown; // ラッシュの後はダッシュのクールダウンに入る
    time.cinematicScale   = 1.0f;              // プレイヤーのスロー打ち消しをやめる（世界スローは呼ぶのをやめれば自然に等速へ戻る）
}

void PlayerDashSystem::SetFlurryVignette(bool on)
{
    // ラッシュ中、画面にビネット（周囲の暗がり）を出す。active が true の間、
    // JustEvadeEffectSystem(R02) が weight を実時間でフェードしてビネットへ反映する。
    if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        fx->flurryActive = on;
}

std::vector<CCL::ECS::TypeID> PlayerDashSystem::GetWriteTypes() const
{
    // テンプレート引数ぶん＋ラッシュのビネット切り替えで書く JustEvadeEffectComponent(singleton)。
    std::vector<CCL::ECS::TypeID> types = IfSystem::GetWriteTypes();
    types.push_back(CCL::ECS::TypeInfo<JustEvadeEffectComponent>::ID());
    return types;
}

// PlayerInputSystem(L01_Input) が書いた移動の要求を読み、物理(L03以降)より前に速度を書く。
REGISTER_LOGIC_SYSTEM(PlayerDashSystem, Priority::LogicStage::L02_Update);
