#include "PlayerCleaveSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h" // ジャスト回避の画面演出をモノクロに使う
#include "Engine/Camera/Components/CameraBodyActionTPSComponent.h"   // カメラへ寄りの要求を書く
#include "Engine/Time/TimeService.h"                                 // 全体スローは Time:: の関数で操作する
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Core/GameEvents.h" // ジャスト回避成立でゲージを足す

#include <DirectXMath.h>
#include <memory>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// ---------------------------------------------------------------------------
// 入口とゲージ
// ---------------------------------------------------------------------------

void PlayerCleaveSystem::Initialize()
{
    // ジャスト回避が成立したらゲージを足す（主な供給源）。満タンになった分はストックに変わる。
    ListenEvent<JustEvadeSuccessEvent>([this](const JustEvadeSuccessEvent &ev) {
        if (auto *cleave = m_world->GetComponent<CleaveComponent>(ev.playerID))
            AddCleaveGauge(*cleave, cleave->gaugeOnJustEvade);
    });
}

void PlayerCleaveSystem::AddCleaveGauge(CleaveComponent &cleave, float amount)
{
    // ストックが上限に達した後は、ゲージを maxGauge で止めて満タンのまま増えないようにする。

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
    // カメラの向きを取る（切り払いの突進はカメラの向いた3D方向へ進むので Y も残す）。
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

    // モノクロ演出(hold)を、今の状態から毎フレーム決めるための集計。
    //   1人でも Ready 中なら true。ForEach の後にこの値で hold を上書きするので、
    //   準備中にプレイヤーが死亡・消滅して状態の更新が止まっても、モノクロやビネットが残り続けない。
    bool anyReady = false;

    // 準備の猶予とクールダウンは実時間(rawDt)で進める。全体スロー中でも同じ秒数で
    // 解除されるようにするため（スローされた localDt を使うと時間が伸びる）。
    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      CleaveComponent            &cleave,
                      PlayerComponent            &player,
                      const PlayerInputComponent &pin,
                      const DashComponent        &dash,
                      const TransformComponent   &trans) {
        // 右クリック=準備 / 左クリック=突進・チェイン（どちらも PlayerInputSystem が押した瞬間だけ true にしている）。
        //   左クリックをここで扱うのは Ready のときだけ（対応表は CleavePhase の宣言を参照）。
        const bool cleavePressed  = pin.cleavePressed;
        const bool releasePressed = pin.attackPressed;

        // RootMotion は実行時に後から付き、無いこともあるので GetComponent で引く。
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);

        // ダッシュを始めたフレームでゲージを少し足す（入力の押下ではなく、ダッシュ状態の変化で判定する）。
        if (dash.isDashing && !cleave.wasDashing)
            AddCleaveGauge(cleave, cleave.gaugeOnDash);
        cleave.wasDashing = dash.isDashing;

        switch (cleave.phase)
        {
        // 待機: 右クリックで準備に入る。クールダウンが終わり、ストックが1つ以上あることが条件。
        case CleavePhase::Idle:
            if (cleave.phaseTimer > 0.0f) cleave.phaseTimer -= rawDt; // クールダウン
            if (cleavePressed && cleave.phaseTimer <= 0.0f && cleave.stock >= 1)
            {
                cleave.chainCount = 0;
                EnterReady(cleave, camAim);
            }
            break;

        // 準備: 狙いを定める。左クリックで突進し、猶予が切れたら待機へ戻る。
        case CleavePhase::Ready:
            cleave.direction = camAim; // 常にカメラの3D方向を狙う
            FaceAim(player, camAim);   // キャラを照準の水平方向へ向ける
            HoldGlobalSlow(cleave);
            cleave.phaseTimer -= rawDt;

            if (releasePressed)
                StartLunge(cleave, camAim);
            else if (cleave.phaseTimer <= 0.0f)
                ExitToIdle(cleave, rm);
            break;

        // 突進
        case CleavePhase::Lunging:
            cleave.phaseTimer -= rawDt;

            ApplyFanKnockback(Vector3(trans.position), cleave);

            if (cleave.phaseTimer <= 0.0f)
            {
                EndLunge(cleave, rm, camAim);
            }
            else
            {
                // 重力を無視して、向いた3D方向へ速度を丸ごと上書きする（上下を含めてまっすぐ突進する）。
                //   上書きの要求は1フレーム限りなので、突進が続くフレームだけ毎回書く。
                //   終了フレームで書かなければ、そのまま通常の移動に戻る。
                //   水平の overrideSpeed は書かない（3D 速度で動かすため）。
                movement.use3DVelocity = true;
                movement.velocity3D    = cleave.direction * cleave.cleaveSpeed;
                if (rm) rm->externallyDriven = true;
            }
            break;
        }

        // このプレイヤーを映すカメラへ、準備中は寄るよう毎フレーム書く（準備外では false を書く）。
        PushCameraCloseUp(id, cleave.phase == CleavePhase::Ready);

        if (cleave.phase == CleavePhase::Ready) anyReady = true;
    });

    // hold は最終的にここで決める（EnterReady/StartLunge/ExitToIdle での設定より優先する）。
    //   準備が途中で止まっても、次のフレームで必ず解除される。
    SetMonochrome(anyReady);
}

// ---------------------------------------------------------------------------
// 構え（スローと白黒）
// ---------------------------------------------------------------------------

void PlayerCleaveSystem::EnterReady(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleavePhase::Ready;
    cleave.phaseTimer = CurrentReadyDuration(cleave);
    cleave.direction  = dir;
    cleave.hitCount   = 0;
    HoldGlobalSlow(cleave);
    SetMonochrome(true);
}

float PlayerCleaveSystem::CurrentSlowScale(const CleaveComponent &cleave) const
{
    int s = cleave.enableStageDecay ? cleave.chainCount : 0;
    if (s < 0)
        s = 0;
    else if (s > 2)
        s = 2;
    const float v[3] = {cleave.slowScale1, cleave.slowScale2, cleave.slowScale3};
    return v[s];
}

float PlayerCleaveSystem::CurrentReadyDuration(const CleaveComponent &cleave) const
{
    int s = cleave.enableStageDecay ? cleave.chainCount : 0;
    if (s < 0)
        s = 0;
    else if (s > 2)
        s = 2;
    const float v[3] = {cleave.readyDuration1, cleave.readyDuration2, cleave.readyDuration3};
    return v[s];
}

void PlayerCleaveSystem::HoldGlobalSlow(const CleaveComponent &cleave)
{
    Time::SustainWorldSlow(*m_world, CurrentSlowScale(cleave), cleave.slowDuration);
}

void PlayerCleaveSystem::ClearGlobalSlow()
{
    Time::ClearWorldSlow(*m_world);
}

void PlayerCleaveSystem::SetMonochrome(bool on)
{
    if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        fx->hold = on;
}

// ---------------------------------------------------------------------------
// 突進
// ---------------------------------------------------------------------------

void PlayerCleaveSystem::FaceAim(PlayerComponent &player, const Vector3 &dir)
{
    player.attackFaceYaw   = std::atan2(dir.x, dir.z); // PlayerMoveSystem と同じ atan2(x,z) の規約
    player.attackFaceTimer = 0.1f;
}

void PlayerCleaveSystem::StartLunge(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleavePhase::Lunging;
    cleave.phaseTimer = cleave.cleaveDuration;
    cleave.direction  = dir;
    cleave.hitCount   = 0;

    // 突進1回でストックを1消費する。準備に入るときに stock>=1 を確認済み。
    if (cleave.stock > 0) cleave.stock--;

    // 突進中は等速・通常色にして、突進を速く見せる。
    ClearGlobalSlow();
    SetMonochrome(false);
}

void PlayerCleaveSystem::EndLunge(CleaveComponent &cleave, RootMotionComponent *rm, const Vector3 &aim)
{
    // TODO(2026-08-13): ドローンの群れを切り開いた先でボスに届くようにする（前進と BREAK）。殻構造の実装と合わせて入れる

    // 3D 速度の上書きは、書くのをやめるだけで解除される（上書きの要求は読まれた時点で戻る）。
    if (rm) rm->externallyDriven = false;

    // 当たっていて、ストックが残っていればチェインを続ける（ストック上限＝チェイン上限）。
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
        // 空振りかストック切れでチェインを終え、クールダウンに入る（スローとモノクロは突進開始時に解除済み）。
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
    // 全体スローは要求をやめるだけで、TimeScaleSystem がイーズで等速へ戻す。
}

// ---------------------------------------------------------------------------
// 演出と吹き飛ばし
// ---------------------------------------------------------------------------

void PlayerCleaveSystem::PushCameraCloseUp(EntityID playerID, bool close)
{
    m_world->ViewEach<CameraBodyTPSComponent>([&](EntityID camID) {
        auto *tps = m_world->GetComponent<CameraBodyTPSComponent>(camID);
        if (!tps || tps->target.id != playerID) return;
        tps->closeUpRequest = close;
    });
}

void PlayerCleaveSystem::ApplyFanKnockback(const Vector3 &origin, CleaveComponent &cleave)
{
    // TODO(2026-08-13): ドローンの数が増えたら、物理エンジンのオーバーラップ判定に置き換える

    const float   rangeSq = cleave.fanRange * cleave.fanRange;
    const float   cosHalf = std::cos(DirectX::XMConvertToRadians(cleave.fanHalfAngle));
    const Vector3 fwd     = cleave.direction; // 正規化済みの3D進行方向（上下を含む）

    // ドローンだけを走査するので、ボスは対象にならない（切り払いは攻撃ではなく、道を開ける技）。
    // 突進中は毎フレーム走るので、機数分の vector を作らない ViewEach を使う。
    m_world->ViewEach<DroneComponent, TransformComponent>([&](EntityID id) {
        auto *drone = m_world->GetComponent<DroneComponent>(id);
        auto *t     = m_world->GetComponent<TransformComponent>(id);
        if (!drone || !t) return;

        // 既に吹き飛ばし中の機体は同じ切り払いで二重ヒットさせない。
        if (drone->knockbackTimer > 0.0f) return;

        // 3D の進行方向を軸にした扇で判定するので、上や下へ突進しても道が開く。
        Vector3     to     = Vector3(t->position) - origin;
        const float distSq = to.LengthSquared();
        if (distSq < 1e-4f || distSq > rangeSq) return;

        Vector3 dir = to;
        dir.Normalize();
        if (dir.Dot(fwd) < cosHalf) return; // 前方扇の外は無視

        // 放射状ではなく進行方向へそろえて吹き飛ばし、開いた道を通れるようにする。
        drone->knockbackVelocity = fwd * cleave.knockbackSpeed;
        drone->knockbackTimer    = cleave.knockbackTime;
        cleave.hitCount++;
    });
}

// PlayerDashSystem(L02_Update) が書いた移動の要求を突進の 3D 速度で上書きし、
// DroneMovementSystem(L03_PrePhysics) より前に吹き飛ばしを書くため、L02_PostUpdate に置く。
REGISTER_LOGIC_SYSTEM(PlayerCleaveSystem, Priority::LogicStage::L02_PostUpdate);
