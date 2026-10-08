// 役割・設計方針は TimeService.h を参照。
#include "Engine/Time/TimeService.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    /**
     * @brief cinematicSlow（全体スローの単一チャネル）に要求を入れる共通処理。
     *
     * 倍率は強い方（scale が小さい）、残り時間は長い方（timer 大）を優先する。
     * 複数の要求が重なっても、どちらかが打ち消されることはない。
     *
     * duration = timer に揃えるので、呼ばれた直後は進行度 0（＝最も遅い状態）から始まる。
     * 呼ぶのをやめると TimeScaleSystem がイーズしながら 1.0 へ戻す。
     *
     * @param ctx     要求を入れる時間の状態。
     * @param scale   スローの倍率。
     * @param seconds 続ける時間（秒）。
     */
    void ArmCinematic(TimeContext &ctx, float scale, float seconds)
    {
        auto      &cs  = ctx.cinematicSlow;
        const bool on  = (cs.timer > 0.0f);
        cs.targetScale = on ? (std::min)(scale, cs.targetScale) : scale;
        const float t  = on ? (std::max)(seconds, cs.timer) : seconds;
        cs.timer       = t;
        cs.duration    = t;
    }
} // namespace

// ---------------------------------------------------------------------------
// ヒットストップとスロー
// ---------------------------------------------------------------------------

void Time::HitStop(World &world, float seconds, float freezeScale)
{
    if (seconds <= 0.0f || !world.HasResource<TimeContext>()) return;
    auto &ctx              = world.GetResource<TimeContext>();
    ctx.globalHitStopTimer = (std::max)(ctx.globalHitStopTimer, seconds); // 長い方を優先
    ctx.globalFreezeScale  = freezeScale;
}

void Time::SlowWorld(World &world, float scale, float seconds)
{
    if (scale >= 1.0f || seconds <= 0.0f || !world.HasResource<TimeContext>()) return;
    ArmCinematic(world.GetResource<TimeContext>(), scale, seconds);
}

void Time::SustainWorldSlow(World &world, float scale, float easeOutSeconds)
{
    if (scale >= 1.0f || !world.HasResource<TimeContext>()) return;
    ArmCinematic(world.GetResource<TimeContext>(), scale, (std::max)(easeOutSeconds, 0.0001f));
}

void Time::ClearWorldSlow(World &world)
{
    if (!world.HasResource<TimeContext>()) return;
    auto &cs                = world.GetResource<TimeContext>().cinematicSlow;
    cs.timer                = 0.0f;
    cs.duration             = 0.0f;
    cs.targetScale          = 1.0f;
    cs.currentComputedScale = 1.0f;
}

void Time::HitStopEntity(World &world, EntityID id, float seconds, float freezeScale)
{
    if (seconds <= 0.0f) return;
    if (auto *t = world.GetComponent<TimeStateComponent>(id))
    {
        t->hitStopTimer = (std::max)(t->hitStopTimer, seconds); // 長い方を優先
        t->freezeScale  = freezeScale;
    }
}

// ---------------------------------------------------------------------------
// ポーズ
// ---------------------------------------------------------------------------

void Time::Pause(World &world)
{
    if (world.HasResource<TimeContext>()) world.GetResource<TimeContext>().isPaused = true;
}

void Time::Resume(World &world)
{
    if (world.HasResource<TimeContext>()) world.GetResource<TimeContext>().isPaused = false;
}

void Time::StepFrame(World &world)
{
    if (world.HasResource<TimeContext>()) world.GetResource<TimeContext>().stepNextFrame = true;
}

// ---------------------------------------------------------------------------
// 見た目用の経過時間
// ---------------------------------------------------------------------------

float Time::PresentationDt(World &world, float realDt)
{
    if (world.HasResource<TimeContext>() && world.GetResource<TimeContext>().isPaused) return 0.0f;
    return realDt;
}
