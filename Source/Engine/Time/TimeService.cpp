// 役割・設計方針は TimeService.h を参照。
#include "Engine/Time/TimeService.h"
#include "Engine/Time/TimeState.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
// cinematicSlow（全体スローの単一チャネル）を武装する共通処理。
//   ・強い方（scale が小さい）を優先、残り時間は長い方（timer 大）を優先＝複数要求が喧嘩しても壊れない。
//   ・duration=timer に揃えるため、武装中は progress=0（＝ピーク維持）。武装をやめると
//     TimeScaleSystem がイーズしながら 1.0 へ戻す。
void ArmCinematic(TimeContext &ctx, float scale, float seconds)
{
    auto      &cs = ctx.cinematicSlow;
    const bool on = (cs.timer > 0.0f);
    cs.targetScale = on ? (std::min)(scale, cs.targetScale) : scale;
    const float t  = on ? (std::max)(seconds, cs.timer) : seconds;
    cs.timer    = t;
    cs.duration = t;
}
} // namespace

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
