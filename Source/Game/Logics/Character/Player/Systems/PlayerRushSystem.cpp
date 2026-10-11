#include "PlayerRushSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Animation/Components/RootMotionComponent.h"          // 踏込/寄せ中はルートモーションの移動を止める
#include "Engine/Time/TimeService.h"                                  // 世界スロー(SustainWorldSlow)・固定フリーズ(HitStop)
#include "Game/Logics/Effects/Components/JustEvadeEffectComponent.h"  // ラッシュのビネット(rushActive)
#include "Game/Logics/Combat/Components/DeadTag.h"                    // 寄せる相手の生存確認

#include <algorithm>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerRushSystem::Update(float rawDt)
{
    // 世界スローの倍率を先に取っておく（プレイヤーだけ実時間へ戻すのに使う）。
    const TimeContext *timeCtx = m_world->HasResource<TimeContext>() ? &m_world->GetResource<TimeContext>() : nullptr;

    ForEachWithID([&](EntityID                    id,
                      CharacterMovementComponent &movement,
                      DashComponent              &dash,
                      RushComponent              &rush,
                      TimeStateComponent         &time,
                      const TransformComponent   &trans) {
        if (!rush.IsActive()) return;

        RootMotionComponent *rootMotion = m_world->GetComponent<RootMotionComponent>(id);

        // 全フェーズ共通：世界スローのままプレイヤーだけ実時間へ戻し、被弾させず、ビネットを立て続ける。
        SustainSlow(rush, time, timeCtx);
        dash.isInvincible = true; // ラッシュ中は被弾させない（反撃のご褒美窓）
        SetVignette(true);        // フェードイン→ピーク維持は JustEvadeEffectSystem が担う

        // ── 踏込(Lunge)：操作ロック・ease-out で回避方向へ前進 ──
        if (rush.phase == RushComponent::Phase::Lunge)
        {
            if (rootMotion) rootMotion->externallyDriven = true; // 踏込で駆動する
            rush.timer -= rawDt;
            const float duration = (rush.lungeDuration > 0.0001f) ? rush.lungeDuration : 0.0001f;
            const float t        = std::clamp((duration - rush.timer) / duration, 0.0f, 1.0f);
            const float factor   = (1.0f - t) * (1.0f - t); // (1-t)^2：出だし速く・終わりで急減速
            movement.desiredVelocity = rush.lungeDir;
            movement.overrideSpeed   = rush.lungeSpeed * factor;
            if (rush.timer <= 0.0f)
            {
                rush.phase = RushComponent::Phase::Plant;
                rush.timer = rush.plantDuration;
                Time::HitStop(*m_world, rush.plantDuration, rush.plantFreeze); // 止まりを別レイヤーで強調
            }
            return;
        }

        // ── 固定(Plant)：操作ロック・全体フリーズで“ピタッ”と見せる ──
        if (rush.phase == RushComponent::Phase::Plant)
        {
            if (rootMotion) rootMotion->externallyDriven = true;
            movement.desiredVelocity = Vector3::Zero; // 入力方向を無効化して固定
            movement.overrideSpeed   = 0.0f;          // 完全停止（フリーズ中はそもそも積分されない）
            rush.timer -= rawDt;                       // フリーズ中も rawDt は進む＝実時間で一定の“溜め”
            if (rush.timer <= 0.0f)
            {
                rush.phase = RushComponent::Phase::Focus;
                rush.timer = rush.focusDuration;
            }
            return;
        }

        // ── 集中(Focus)：操作自由。攻撃で対象へ寄れる。スローは時間で終わる＝連撃できる窓 ──
        rush.timer -= rawDt;
        if (rush.timer <= 0.0f)
        {
            End(dash, rush, time); // 時間経過で終了。ビネットもここで降ろす
            if (rootMotion) rootMotion->externallyDriven = false;
            return;
        }

        // 寄せ中のフレームだけ移動を上書き（PlayerDashSystem は LocksMovement() を見てスキップする）。
        if (DriveCloseIn(id, Vector3(trans.position), rush, movement, rootMotion, rawDt))
            return;

        // 寄せていないフレームは自由。移動/ダッシュ/攻撃は PlayerDashSystem と攻撃Systemが担う。
        if (rootMotion) rootMotion->externallyDriven = false;
    });
}

void PlayerRushSystem::SustainSlow(RushComponent &rush, TimeStateComponent &time, const TimeContext *timeCtx)
{
    // cinematicScale = playerScale / worldScale とすると localDt の中で世界スローが約分され、
    //   プレイヤーの時計だけが playerScale（既定 1.0＝実時間）で進む。
    Time::SustainWorldSlow(*m_world, rush.worldScale, rush.slowRecover);

    // 除数の下限を worldScale で止めるのは、固定(Plant)の全体フリーズで world が 0 近くへ落ちても
    //   cinematicScale が頭打ちになり、フリーズ解除フレームで localDt が巨大化してプレイヤーが
    //   暴走するのを防ぐため（フリーズ中は globalScale=0 なので localDt=0＝プレイヤーは固定のまま）。
    const float world      = timeCtx ? (timeCtx->globalScale * timeCtx->groupScales[static_cast<size_t>(time.group)]) : 1.0f;
    const float worldFloor = (std::max)(world, rush.worldScale);
    time.cinematicScale    = rush.playerScale / worldFloor;
}

bool PlayerRushSystem::DriveCloseIn(EntityID id, const Vector3 &playerPos, RushComponent &rush,
                                    CharacterMovementComponent &movement, RootMotionComponent *rootMotion, float rawDt)
{
    if (rush.closeInTimer <= 0.0f) return false; // 寄せ要求なし

    rush.closeInTimer -= rawDt; // 実時間で計る（スローに引きずられない）

    // 対象が消えていたら寄せをやめる。
    if (!m_world->IsEntityValid(rush.closeInTargetID) || m_world->HasComponent<Tag::DeadTag>(rush.closeInTargetID))
    {
        rush.closeInTimer = 0.0f;
        return false;
    }

    // 対象へ毎フレーム方向を取り直して寄せる（軽いホーミング）。
    Vector3 toTarget = Vector3::Zero;
    if (const auto *etrans = m_world->GetComponent<TransformComponent>(rush.closeInTargetID))
    {
        toTarget   = Vector3(etrans->position) - playerPos;
        toTarget.y = 0.0f;
    }

    const float distance = toTarget.Length();
    if (distance <= rush.closeInStopRange)
    {
        rush.closeInTimer = 0.0f; // 間合いに入った＝寄せ完了。以降は自由（攻撃はそのまま出る）
        return false;
    }

    toTarget /= distance; // 正規化（distance>0 が保証されている）
    rush.lungeDir            = toTarget; // 向きの基準として残す
    movement.desiredVelocity = toTarget;
    movement.overrideSpeed   = rush.closeInSpeed;
    if (rootMotion) rootMotion->externallyDriven = true; // 寄せはこちらで駆動する
    return true;                                         // 寄せ中はこのフレーム移動を上書き
}

void PlayerRushSystem::End(DashComponent &dash, RushComponent &rush, TimeStateComponent &time)
{
    rush.phase           = RushComponent::Phase::None;
    rush.timer           = 0.0f;
    rush.closeInTimer    = 0.0f;
    rush.closeInTargetID = InvalidEntityID;
    time.cinematicScale  = 1.0f; // プレイヤーのスロー打ち消しをやめる（世界スローはイーズで自然に戻る）
    SetVignette(false);          // フェードアウト開始

    // 集中スロー中に再ダッシュしていたら、そのダッシュの残り時間・無敵を壊さない。
    //   ダッシュせずに終えた通常時だけ、ラッシュ後のクールダウンへ入れ、無敵を解く。
    if (!dash.isDashing)
    {
        dash.isInvincible = false;
        dash.currentTimer = dash.dashCooldown;
    }
}

void PlayerRushSystem::SetVignette(bool on)
{
    // ラッシュ中、画面にビネット（周囲の暗がり）を出す。rushActive の間、
    // JustEvadeEffectSystem(R02) が weight を実時間でフェードしてビネットへ反映する。
    if (auto *fx = m_world->GetSingletonComponent<JustEvadeEffectComponent>())
        fx->rushActive = on;
}

std::vector<CCL::ECS::TypeID> PlayerRushSystem::GetWriteTypes() const
{
    // テンプレート引数ぶん＋ビネット切り替えで書く JustEvadeEffectComponent(singleton)。
    std::vector<CCL::ECS::TypeID> types = IfSystem::GetWriteTypes();
    types.push_back(CCL::ECS::TypeInfo<JustEvadeEffectComponent>::ID());
    return types;
}

// ラッシュの駆動は PlayerDashSystem より前に走らせる（ロックするフレームは Dash 側がスキップする）。
REGISTER_LOGIC_SYSTEM(PlayerRushSystem, Priority::LogicStage::L02_Update);
