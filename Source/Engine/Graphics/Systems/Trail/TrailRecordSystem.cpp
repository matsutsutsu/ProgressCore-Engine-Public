#include "TrailRecordSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Time/Components/TimeState.h"

using namespace CCL::ECS;

void TrailRecordSystem::Update(float dt)
{
    const float worldDt = WorldDt(dt);

    ForEachWithID([&](EntityID id, TrailComponent &trail) {
        // 処理フロー:
        // 1. 2つの時計で、このステップに進む時間を決める
        // 2. 一時トレイルのタイマーを進める
        // 3. 断面を老化させ、寿命切れを捨てる
        // 4. 描画側へ刻んでよい印を出す

        // ---------------------------------------------------------
        // 1. 時計
        // ---------------------------------------------------------

        // 一時トレイルは持ち主の時計（理由は TrailRecordSystem.h）。
        const float ownerDt = ClockDt(trail.ownerEntity, ClockDt(id, worldDt));
        const float emitDt  = ownerDt;

        // 老化は、animControlled なら持ち主の時計、それ以外はトレイル自身の時計。
        // 剣の尾はアニメーションが描く軌跡なので、アニメーションと同じ時計で消えないと、
        // ラッシュ中（世界だけスロー）に尾がプレイヤーの動きより何倍も長く残ってしまう。
        const float lifeDt = trail.animControlled ? ownerDt : ClockDt(id, worldDt);

        trail.timeScale = (dt > 0.0f) ? (lifeDt / dt) : 0.0f;

        // ---------------------------------------------------------
        // 2. 一時トレイル
        // ---------------------------------------------------------

        // 切れると emitting が落ちるので、印を立てる前に進める
        trail.UpdateBurstTimer(emitDt);

        // ---------------------------------------------------------
        // 3. 老化
        // ---------------------------------------------------------

        // これを描画側で行うと、尾の長さがリフレッシュレートで変わってしまう。
        const int MAX = TrailComponent::MAX_SEGMENTS;

        for (int i = 0; i < trail.count; ++i)
        {
            const int idx = (trail.head + i) % MAX;
            trail.segments[idx].age += lifeDt;
        }

        while (trail.count > 0 && trail.segments[trail.head].age >= trail.lifeTime)
        {
            trail.head = (trail.head + 1) % MAX;
            trail.count--;
        }

        // ---------------------------------------------------------
        // 4. 刻む印
        // ---------------------------------------------------------

        // スロー中も毎ステップ立てる。
        // 刃を動かしているのは持ち主の時計なので、トレイルの時計に合わせて間引くと、
        // ラッシュのように持ち主だけ実時間で動くときに尾が粗い折れ線になる。
        // 実際に刻むかどうかは、TrailSystem が先端の移動量（minDistance）で決める。
        if (trail.emitting)
        {
            trail.sampleRequested = true;
        }
    });
}

float TrailRecordSystem::ClockDt(EntityID id, float fallbackDt) const
{
    if (id == InvalidEntityID)
    {
        return fallbackDt;
    }

    const auto *time = m_world->GetComponent<TimeStateComponent>(id);
    return time ? time->localDt : fallbackDt;
}

float TrailRecordSystem::WorldDt(float dt) const
{
    if (!m_world->HasResource<TimeContext>())
    {
        return dt;
    }

    const auto &ctx = m_world->GetResource<TimeContext>();
    return ctx.isPaused ? 0.0f : dt * ctx.globalScale;
}

// emitting と ownerEntity を決める TrailAnimationSyncSystem（L05）の後
REGISTER_LOGIC_SYSTEM(TrailRecordSystem, Priority::LogicStage::L06_Resolution);
