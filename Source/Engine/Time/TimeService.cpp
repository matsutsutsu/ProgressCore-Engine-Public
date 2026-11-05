// 役割・設計方針は TimeService.h を参照
#include "Engine/Time/TimeService.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // 全体のスロー（cinematicSlow）に要求を入れる。倍率は強い方（scale が小さい）、残り時間は長い方を残す
    // duration を timer にそろえるので、呼んだ直後は一番遅い状態から始まる。呼ぶのをやめると TimeScaleSystem が 1.0 へ戻す
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
