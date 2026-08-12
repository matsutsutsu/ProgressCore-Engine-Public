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
#include "Game/Logics/AI/Drone/DroneComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"

#include <DirectXMath.h>
#include <memory>
#include <cmath>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerCleaveSystem::Update(float rawDt)
{
    // 入力：右クリック=準備(Cleave)、左クリック=突進(Attack)。エッジは各エンティティで検出する。
    bool cleaveHeld = false;
    bool attackHeld = false;
    if (m_world->HasResource<std::shared_ptr<IInputAPI>>())
    {
        auto in    = m_world->GetResource<std::shared_ptr<IInputAPI>>();
        cleaveHeld = in->GetAction("Cleave");
        attackHeld = in->GetAction("Attack");
    }

    // 照準＝カメラ前方。突進は「向いている方向」へ上下含めて飛ぶので Y も残した3Dベクトルを使う
    // （上を向いていれば上、下なら下へ）。準備中は毎フレーム更新する。
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
    // 解除されるようにするため（スローされた localDt を使うと窓が伸びてしまう）。
    ForEachWithID([&](EntityID                        id,
                      CharacterMovementInputComponent &input,
                      CleaveComponent                 &cleave,
                      const TransformComponent        &trans) {
        RootMotionComponent *rm = m_world->GetComponent<RootMotionComponent>(id);
        PlayerComponent     *pc = m_world->GetComponent<PlayerComponent>(id);

        // 入力エッジ（押した瞬間だけ true）。
        const bool cleaveEdge  = cleaveHeld && !cleave.wasCleavePressed;
        const bool releaseEdge = attackHeld && !cleave.wasReleasePressed;
        cleave.wasCleavePressed  = cleaveHeld;
        cleave.wasReleasePressed = attackHeld;

        switch (cleave.phase)
        {
        case CleaveComponent::Phase::Idle:
            if (cleave.phaseTimer > 0.0f) cleave.phaseTimer -= rawDt; // クールダウン
            if (cleaveEdge && cleave.phaseTimer <= 0.0f)
            {
                cleave.chainCount = 0;
                EnterReady(cleave, camAim);
            }
            break;

        case CleaveComponent::Phase::Ready:
            cleave.direction = camAim;      // 常にカメラの3D方向を狙う
            if (pc) FaceAim(*pc, camAim);   // 「向いている方向」を（水平に）見せる
            HoldGlobalSlow(cleave);         // 全体スローを維持
            cleave.phaseTimer -= rawDt;

            if (releaseEdge)
                StartLunge(cleave, camAim); // 左クリック→突進
            else if (cleave.phaseTimer <= 0.0f)
                ExitToIdle(cleave, input, rm);  // 放置→自動解除
            break;

        case CleaveComponent::Phase::Lunging:
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
        PushCameraCloseUp(id, cleave.phase == CleaveComponent::Phase::Ready);
    });
}

void PlayerCleaveSystem::EnterReady(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleaveComponent::Phase::Ready;
    cleave.phaseTimer = cleave.readyDuration;
    cleave.direction  = dir;
    cleave.hitCount   = 0;
    HoldGlobalSlow(cleave); // 全体スロー開始
    SetMonochrome(true);    // 画面モノクロ（時間が止まった実感）
}

// TimeContext.cinematicSlow を毎フレーム満タンに張り直して「準備中ずっと全体スロー」を維持する。
// cinematicSlow は globalScale に乗るため group に依らず全エンティティ（自機=group:None 含む）が
// 遅くなる。準備を抜けたら張り直しをやめる→timer が自然に減り TimeScaleSystem がイーズで等速へ戻す。
void PlayerCleaveSystem::HoldGlobalSlow(const CleaveComponent &cleave)
{
    if (cleave.slowDuration <= 0.0f || cleave.slowScale >= 1.0f) return; // 無効設定
    if (!m_world->HasResource<TimeContext>()) return;

    auto &ctx                     = m_world->GetResource<TimeContext>();
    ctx.cinematicSlow.timer       = cleave.slowDuration;
    ctx.cinematicSlow.duration    = cleave.slowDuration;
    ctx.cinematicSlow.targetScale = cleave.slowScale;
}

// 突進の瞬間に等速へ「スナップ」させる（イーズを待たず即 1.0 へ）。
void PlayerCleaveSystem::ClearGlobalSlow()
{
    if (!m_world->HasResource<TimeContext>()) return;
    auto &ctx = m_world->GetResource<TimeContext>();
    ctx.cinematicSlow.timer                = 0.0f;
    ctx.cinematicSlow.currentComputedScale = 1.0f;
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

void PlayerCleaveSystem::StartLunge(CleaveComponent &cleave, const Vector3 &dir)
{
    cleave.phase      = CleaveComponent::Phase::Lunging;
    cleave.phaseTimer = cleave.cleaveDuration;
    cleave.direction  = dir;
    cleave.hitCount   = 0;
    // 突進中は通常速度・通常色にする（切り込みを速く見せる）。
    ClearGlobalSlow();
    SetMonochrome(false);
}

void PlayerCleaveSystem::EndLunge(CleaveComponent &cleave, CharacterMovementInputComponent &input,
                                  RootMotionComponent *rm, const Vector3 &aim)
{
    input.overrideSpeed = -1.0f;
    input.use3DVelocity = false; // 3D突進を解除（以後は通常の重力・水平移動へ戻す）
    if (rm) rm->externallyDriven = false;

    if (cleave.hitCount > 0)
    {
        // 当てた → 連鎖継続。再び準備(全体スロー＋モノクロ)へ戻し、左クリックで次の切り払いへ。
        cleave.chainCount++;
        CCL_LOG_INFO(LogCategory::Game, "[Cleave] hit! chain=%d (hits=%d) -> Ready", cleave.chainCount, cleave.hitCount);
        EnterReady(cleave, aim);
    }
    else
    {
        // 空振り → 連鎖終了。クールダウンへ（スロー/モノクロは既に突進開始時に解除済み）。
        CCL_LOG_INFO(LogCategory::Game, "[Cleave] whiff -> end. chain was %d", cleave.chainCount);
        cleave.phase      = CleaveComponent::Phase::Idle;
        cleave.phaseTimer = cleave.cleaveCooldown;
        cleave.chainCount = 0;
    }
}

void PlayerCleaveSystem::ExitToIdle(CleaveComponent &cleave, CharacterMovementInputComponent &input, RootMotionComponent *rm)
{
    cleave.phase        = CleaveComponent::Phase::Idle;
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
