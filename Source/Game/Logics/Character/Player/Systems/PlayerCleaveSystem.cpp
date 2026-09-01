// 役割・設計方針は PlayerCleaveSystem.h を参照。
#include "PlayerCleaveSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h" // 画面モノクロ演出を流用
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

    // ★ hold(モノクロ演出)を実状態から確定させるための集計。
    //   誰か1人でも Ready 中なら true。ForEach 後にこの値で hold を上書きするため、
    //   準備中にプレイヤーが死亡/消滅して FSM が回らなくなっても hold が立ちっぱなしに
    //   ならない（＝「何もないのにモノクロ/ビネットが残り続ける」不具合の根治）。
    bool anyReady = false;

    // 準備の猶予・クールダウンは「実時間(rawDt)」で進める。全体スロー中でも一定の秒数で
    // 解除されるようにするため（スローされた localDt を使うと時間が伸びてしまう）。
    ForEachWithID([&](EntityID                        id,
                      CharacterMovementComponent &movement,
                      CleaveComponent                 &cleave,
                      PlayerComponent                 &player,
                      const PlayerInputComponent      &pin,
                      const DashComponent             &dash,
                      const TransformComponent        &trans)
        {
        // 入力の意思（PlayerInputSystem が押した瞬間まで解決済み）：
        //   右クリック=準備 / 左クリック=突進・チェイン。
        //   左クリックは Ready のときだけここへ届く（対応表は CleavePhase の宣言を参照）。
        const bool cleavePressed  = pin.cleavePressed;
        const bool releasePressed = pin.attackPressed;

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
                ExitToIdle(cleave, rm);  // 放置→自動解除
            break;

        // 突進状態：突進中のフェーズ。
        case CleavePhase::Lunging:
            cleave.phaseTimer -= rawDt;

            ApplyFanKnockback(Vector3(trans.position), cleave);

            if (cleave.phaseTimer <= 0.0f)
            {
                EndLunge(cleave, rm, camAim);
            }
            else
            {
                // 重力を無視して「向いた3D方向」へ丸ごと速度を上書き＝上下含めて真っ直ぐ突進。
                //   上書き要求は1フレーム限りなので、突進が続くフレームだけ出し直す。
                //   終了フレームで出さないことが、そのまま通常移動への復帰になる。
                //   水平の overrideSpeed は書かない（3D側で駆動するため）。
                movement.use3DVelocity = true;
                movement.velocity3D    = cleave.direction * cleave.cleaveSpeed;
                if (rm) rm->externallyDriven = true;
            }
            break;
        }

        // このプレイヤーを映すカメラへ「準備中は寄る」を毎フレーム伝える（未要求へ自動リセット）。
        PushCameraCloseUp(id, cleave.phase == CleavePhase::Ready);

        if (cleave.phase == CleavePhase::Ready) anyReady = true;
    });

    // ★ hold を毎フレーム実状態で確定させる（EnterReady/StartLunge/ExitToIdle の
    //   個別呼び出しに依存せず、ここが唯一の真実）。準備が中断されても確実に解除される。
    SetMonochrome(anyReady);
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
void PlayerCleaveSystem::EndLunge(CleaveComponent &cleave, RootMotionComponent *rm, const Vector3 &aim)
{
    // 3D突進の解除は「出し直すのをやめる」だけでよい（上書き要求は読まれた時点で戻る）。
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

void PlayerCleaveSystem::ExitToIdle(CleaveComponent &cleave, RootMotionComponent *rm)
{
    cleave.phase      = CleavePhase::Idle;
    cleave.phaseTimer = cleave.cleaveCooldown;
    cleave.chainCount = 0;
    if (rm) rm->externallyDriven = false;
    SetMonochrome(false);
    // 全体スローの張り直しをやめるだけで、TimeScaleSystem が自然にイーズで等速へ戻す。
}

// PlayerDashSystem::PushWideFovRequest と同じ方式：このプレイヤーを映す TPS カメラに
// 寄り要求だけを伝える（カメラは切り払いの事情を知らない）。
void PlayerCleaveSystem::PushCameraCloseUp(EntityID playerID, bool close)
{
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) return;
        tps->closeUpRequest = close;
    });
}

// 前方扇の範囲内にいるドローンを進行方向へ吹き飛ばす。
// TODO: 距離ベースで全ドローンを走査している。数が増えたら物理エンジン(オーバーラップ)経由に置き換えたい。
//       走査自体は ViewEach で確保なしにしてあるので、残るコストは距離計算のみ。
void PlayerCleaveSystem::ApplyFanKnockback(const Vector3 &origin, CleaveComponent &cleave)
{
    const float rangeSq = cleave.fanRange * cleave.fanRange;
    const float cosHalf = std::cos(DirectX::XMConvertToRadians(cleave.fanHalfAngle));
    const Vector3 fwd   = cleave.direction; // 正規化済み（3D：上下含む進行方向）

    // ドローンのみ走査＝ボスは自動的に対象外（切り払いは攻撃ではなく「道を作る技」）。
    // 突進中は毎フレーム走るので、機数ぶんの vector を作らない ViewEach を使う。
    m_world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) {
        auto *drone = m_world->GetComponent<DroneComponent>(id);
        auto *t     = m_world->GetComponent<TransformComponent>(id);
        if (!drone || !t) return;

        // 既に吹き飛ばし中の機体は同じ切り払いで二重ヒットさせない。
        if (drone->knockbackTimer > 0.0f) return;

        // 進行方向(3D)を軸にした扇で判定＝上下に斬り上げ/斬り下げても道が開く。
        Vector3 to = Vector3(t->position) - origin;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > rangeSq) return;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(fwd) < cosHalf) return; // 前方扇の外は無視

        // 進行方向へ一律に吹き飛ばす（放射状にしない）＝「切り開いた道は通れる」保証。
        drone->knockbackVelocity = fwd * cleave.knockbackSpeed;
        drone->knockbackTimer    = cleave.knockbackTime;
        cleave.hitCount++;
    });
}

// PlayerDashSystem(L02_Update) の後で overrideSpeed を上書きし、DroneMovementSystem
// (L03_PrePhysics) より前に吹き飛ばしを書くため、L02_PostUpdate に置く。
REGISTER_LOGIC_SYSTEM(PlayerCleaveSystem, Priority::LogicStage::L02_PostUpdate);
