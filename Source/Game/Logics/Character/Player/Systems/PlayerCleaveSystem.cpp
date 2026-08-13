/// @file PlayerCleaveSystem.cpp
/// 役割・設計方針は PlayerCleaveSystem.h を参照。
#include "PlayerCleaveSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Input/IInputAPI.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h" // 画面モノクロ演出を流用
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"      // 寄り(クローズアップ)要求をpush
#include "Engine/Time/TimeService.h"                                   // 全体スローは意図ファサード経由
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/GameEvent/GameEvents.h"                          // ゲージ供給：ジャスト回避イベント

#include <DirectXMath.h>
#include <memory>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerCleaveSystem::Initialize()
{
    // ジャスト回避成立でゲージを多めに加算（主供給）。満タンぶんはストックへ変換される。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        if (auto *cleave = m_world->GetComponent<CleaveComponent>(ev.playerID))
            AddCleaveGauge(*cleave, cleave->gaugeOnJustEvade);
    });
}

// ゲージを加算し、maxGauge を超えたぶんを 1 ストックへ両替する（上限 maxStock）。
// 上限到達後はゲージを maxGauge で頭打ちにして「満タン表示のまま増えない」状態にする。
void PlayerCleaveSystem::AddCleaveGauge(CleaveComponent &cleave, float amount)
{
    if (amount <= 0.0f || cleave.maxGauge <= 0.0f) return;
    cleave.gauge += amount;
    while (cleave.gauge >= cleave.maxGauge && cleave.stock < cleave.maxStock)
    {
        cleave.gauge -= cleave.maxGauge;
        cleave.stock++;
    }
    if (cleave.stock >= cleave.maxStock && cleave.gauge > cleave.maxGauge)
        cleave.gauge = cleave.maxGauge;
}

void PlayerCleaveSystem::Update(float rawDt)
{
    // 入力：右クリック=準備(Cleave)、左クリック=突進(Attack)。
    // どちらも InputConfig で "Triggered" 登録なので GetAction が「押した瞬間の1フレーム」を返す。
    bool cleavePressed  = false;
    bool releasePressed = false;
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto input     = m_world->GetResource<std::shared_ptr<IInputAPI>>();
        cleavePressed  = input->GetAction("Cleave");
        releasePressed = input->GetAction("Attack");
    }

    // 向いている方向を取得する（CLEAVE の突進はカメラの向いた3D方向へ進むため Y を残す）。
    Vector3 camAim = Vector3::UnitZ;
    if (m_world->HasResource<Camera *>())
    {
        if (Camera *cam = m_world->GetResource<Camera *>())
        {
            camAim = cam->GetFront();
            if (camAim.LengthSquared() > 0.001f)
                camAim.Normalize();
            else
                camAim = Vector3::UnitZ;
        }
    }

    // 準備の猶予・クールダウンは「実時間(rawDt)」で進める。全体スロー中でも一定の秒数で
    // 解除されるようにするため（スローされた localDt を使うと時間が伸びてしまう）。
    ForEachWithID([&](EntityID                        id,
                      CharacterMovementInputComponent &input,
                      CleaveComponent                 &cleave,
                      PlayerComponent                 &player,
                      const DashComponent             &dash,
                      const TransformComponent        &trans)
        {
        // RootMotion は実行時後付け・任意依存なのでここだけ GetComponent（無ければ nullptr）。
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);

        // 通常回避(ダッシュ)開始の立ち上がりで救済ゲージを少量加算（※これは入力でなくダッシュ状態のエッジ）。
        if (dash.isDashing && !cleave.wasDashing)
            AddCleaveGauge(cleave, cleave.gaugeOnDash);
        cleave.wasDashing = dash.isDashing;

        switch (cleave.phase)
        {
        // 通常状態：CLEAVE を使っていない時のフェーズ。右クリックで準備に入る。
        case CleavePhase::Idle:
            if (cleave.phaseTimer > 0.0f) cleave.phaseTimer -= rawDt; // クールダウン
            // クールタイムが終わっていて、ストックが1つ以上あれば準備状態へ。
            if (cleavePressed && cleave.phaseTimer <= 0.0f && cleave.stock >= 1)
            {
                cleave.chainCount = 0;
                EnterReady(cleave, camAim);
            }
            break;

        // 準備状態：狙いを定めるフェーズ。左クリックで突進、放置で自動解除。
        case CleavePhase::Ready:
            cleave.direction = camAim;    // 常にカメラの3D方向を狙う
            FaceAim(player, camAim);      // 「向いている方向」を（水平に）見せる
            HoldGlobalSlow(cleave);       // 全体スローを維持
            cleave.phaseTimer -= rawDt;

            if (releasePressed)
                StartLunge(cleave, camAim); // 左クリック→突進
            else if (cleave.phaseTimer <= 0.0f)
                ExitToIdle(cleave, input, rm);  // 放置→自動解除
            break;

        // 突進状態：突進中のフェーズ。
        case CleavePhase::Lunging:
            cleave.phaseTimer -= rawDt;

            // 重力を無視して「向いた3D方向」へ丸ごと速度を上書き＝上下含めて真っ直ぐ突進。
            input.use3DVelocity = true;
            input.velocity3D    = cleave.direction * cleave.cleaveSpeed;
            input.overrideSpeed = -1.0f;   // 水平上書きは使わない（3D側で駆動）
            input.dashRequested = false;   // 突進中はダッシュ入力を握り潰す
            if (rm) rm->externallyDriven = true;

            ApplyFanKnockback(Vector3(trans.position), cleave);

            if (cleave.phaseTimer <= 0.0f)
                EndLunge(cleave, input, rm, camAim);
            break;
        }

        // このプレイヤーを映すカメラへ「準備中は寄る」を毎フレーム伝える（未要求へ自動リセット）。
        PushCameraCloseUp(id, cleave.phase == CleavePhase::Ready);
    });
}

// 準備状態に入る。照準方向をセットし、全体スロー＋モノクロを開始する。
void PlayerCleaveSystem::EnterReady(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleavePhase::Ready;
    cleave.phaseTimer = CurrentReadyDuration(cleave); // 段ごとの固定猶予
    cleave.direction  = dir;
    cleave.hitCount   = 0;
    HoldGlobalSlow(cleave); // 全体スロー開始（段ごとの固定倍率）
    SetMonochrome(true);    // 画面モノクロ（時間が止まった実感）
}

// チェイン段数(0/1/2)で段ごとの固定スロー倍率を引く。段が上がるほど倍率↑＝危険が迫る。
float PlayerCleaveSystem::CurrentSlowScale(const CleaveComponent &cleave) const
{
    int s = cleave.enableStageDecay ? cleave.chainCount : 0;
    if (s < 0) s = 0; else if (s > 2) s = 2;
    const float v[3] = { cleave.slowScale1, cleave.slowScale2, cleave.slowScale3 };
    return v[s];
}

// チェイン段数(0/1/2)で段ごとの固定準備猶予を引く。段が上がるほど短い＝迷える時間が減る。
float PlayerCleaveSystem::CurrentReadyDuration(const CleaveComponent &cleave) const
{
    int s = cleave.enableStageDecay ? cleave.chainCount : 0;
    if (s < 0) s = 0; else if (s > 2) s = 2;
    const float v[3] = { cleave.readyDuration1, cleave.readyDuration2, cleave.readyDuration3 };
    return v[s];
}

// 準備中の全体スローを維持する。段ごとの固定倍率（CurrentSlowScale）を「このフレーム分」要求する。
// 呼ぶのをやめる（＝準備を抜ける）と slowDuration 秒でイーズ復帰する。
void PlayerCleaveSystem::HoldGlobalSlow(const CleaveComponent &cleave)
{
    Time::SustainWorldSlow(*m_world, CurrentSlowScale(cleave), cleave.slowDuration);
}

// 突進の瞬間に等速へ「スナップ」させる（イーズを待たず即 1.0 へ）。
void PlayerCleaveSystem::ClearGlobalSlow()
{
    Time::ClearWorldSlow(*m_world);
}

// ジャスト回避の演出singleton(JustEvadeEffectComponent)を hold で流用し、準備中だけモノクロ保持。
void PlayerCleaveSystem::SetMonochrome(bool on)
{
    if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        fx->hold = on;
}

// 攻撃時オートフェイシングと同じ経路(attackFaceYaw/Timer)を使い、PlayerMoveSystem に
// 照準方向へ回頭させる。毎フレーム短いタイマーを更新し続けて「狙い方向を向く」を維持。
void PlayerCleaveSystem::FaceAim(PlayerComponent &player, const Vector3 &dir)
{
    player.attackFaceYaw   = std::atan2(dir.x, dir.z); // PlayerMoveSystem と同じ atan2(x,z) 規約
    player.attackFaceTimer = 0.1f;
}

// 突進開始：突進フェーズへ移行し、ストックを1消費。スロー/モノクロを解除して等速・通常色にする。
void PlayerCleaveSystem::StartLunge(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleavePhase::Lunging;
    cleave.phaseTimer = cleave.cleaveDuration;
    cleave.direction  = dir;
    cleave.hitCount   = 0;

    // 切り払い（突進1回）でストックを1消費。準備突入時に stock>=1 を保証済み。
    if (cleave.stock > 0) cleave.stock--;

    // 突進中は通常速度・通常色にする（切り込みを速く見せる）。
    ClearGlobalSlow();
    SetMonochrome(false);
}

// 突進終了：ヒットありかつ残ストックがあれば再準備で連鎖、そうでなければ解除しクールダウンへ。
// （※「群れを切り開いた末にボスへ届く」因果の担保＝前進/BREAK は殻構造の実装時に導入する）
void PlayerCleaveSystem::EndLunge(CleaveComponent &cleave, CharacterMovementInputComponent &input,
                                  RootMotionComponent *rm, const Vector3 &aim)
{
    input.overrideSpeed = -1.0f;
    input.use3DVelocity = false; // 3D突進を解除（以後は通常の重力・水平移動へ戻す）
    if (rm) rm->externallyDriven = false;

    // 連鎖にはストック残が要る（ストック上限＝チェイン上限）。ヒット＋残ストックで継続。
    const bool chain = (cleave.hitCount > 0) && (cleave.stock >= 1);

    if (chain)
    {
        cleave.chainCount++;
        CCL_LOG_INFO(LogCategory::Game, "[Cleave] chain=%d (hits=%d, stock=%d) -> Ready",
                     cleave.chainCount, cleave.hitCount, cleave.stock);
        EnterReady(cleave, aim);
    }
    else
    {
        // 空振り／ストック切れ → 連鎖終了。クールダウンへ（スロー/モノクロは突進開始時に解除済み）。
        CCL_LOG_INFO(LogCategory::Game, "[Cleave] end (hits=%d, stock=%d). chain was %d",
                     cleave.hitCount, cleave.stock, cleave.chainCount);
        cleave.phase      = CleavePhase::Idle;
        cleave.phaseTimer = cleave.cleaveCooldown;
        cleave.chainCount = 0;
    }
}

void PlayerCleaveSystem::ExitToIdle(CleaveComponent &cleave, CharacterMovementInputComponent &input, RootMotionComponent *rm)
{
    cleave.phase        = CleavePhase::Idle;
    cleave.phaseTimer   = cleave.cleaveCooldown;
    cleave.chainCount   = 0;
    input.overrideSpeed = -1.0f;
    input.use3DVelocity = false;
    if (rm) rm->externallyDriven = false;
    SetMonochrome(false);
    // 全体スローの張り直しをやめるだけで、TimeScaleSystem が自然にイーズで等速へ戻す。
}

// PlayerDashSystem::PushWideFovRequest と同じ方式：このプレイヤーを映す TPS カメラに
// 寄り要求だけを伝える（カメラは切り払いの事情を知らない）。
void PlayerCleaveSystem::PushCameraCloseUp(EntityID playerID, bool close)
{
    for (EntityID camID : m_world->View<CameraBodyTPSComponent>())
    {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) continue;
        tps->closeUpRequest = close;
    }
}

// 前方扇の範囲内にいるドローンを進行方向へ吹き飛ばす。
// TODO: 距離ベースで全ドローンを走査している。数が増えたら物理エンジン(オーバーラップ)経由に置き換えたい。
void PlayerCleaveSystem::ApplyFanKnockback(const Vector3 &origin, CleaveComponent &cleave)
{
    const float rangeSq = cleave.fanRange * cleave.fanRange;
    const float cosHalf = std::cos(DirectX::XMConvertToRadians(cleave.fanHalfAngle));
    const Vector3 fwd   = cleave.direction; // 正規化済み（3D：上下含む進行方向）

    // ドローンのみ走査＝ボスは自動的に対象外（切り払いは攻撃ではなく「道を作る技」）。
    for (EntityID id : m_world->View<DroneComponent, TransformComponent>())
    {
        auto *drone = m_world->GetComponent<DroneComponent>(id);
        auto *t     = m_world->GetComponent<TransformComponent>(id);
        if (!drone || !t) continue;

        // 既に吹き飛ばし中の機体は同じ切り払いで二重ヒットさせない。
        if (drone->knockbackTimer > 0.0f) continue;

        // 進行方向(3D)を軸にした扇で判定＝上下に斬り上げ/斬り下げても道が開く。
        Vector3 to = Vector3(t->position) - origin;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > rangeSq) continue;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(fwd) < cosHalf) continue; // 前方扇の外は無視

        // 進行方向へ一律に吹き飛ばす（放射状にしない）＝「切り開いた道は通れる」保証。
        drone->knockbackVelocity = fwd * cleave.knockbackSpeed;
        drone->knockbackTimer    = cleave.knockbackTime;
        cleave.hitCount++;
    }
}

// PlayerDashSystem(L02_Update) の後で overrideSpeed を上書きし、DroneMovementSystem
// (L03_PrePhysics) より前に吹き飛ばしを書くため、L02_PostUpdate に置く。
REGISTER_LOGIC_SYSTEM(PlayerCleaveSystem, Priority::LogicStage::L02_PostUpdate);
