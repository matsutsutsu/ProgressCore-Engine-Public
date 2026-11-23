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
        // 1. このステップに進む時間を、2つの時計から決める
        // 2. 一時トレイルのタイマーを進める
        // 3. 断面を老化させ、寿命切れを捨てる
        // 4. 描画側へ刻んでいい印を出す

        // ---------------------------------------------------------
        // 1. 時計
        // ---------------------------------------------------------

        // トレイル自身の時計。時計を持たなければ世界の時計
        const float selfDt = ClockDt(id, worldDt);

        // 持ち主の時計。持ち主がいないか、時計を持たなければ selfDt
        const float ownerDt = ClockDt(trail.ownerEntity, selfDt);

        // 老化は、アニメ制御なら持ち主の時計、それ以外はトレイル自身の時計で進める
        // 剣の尾はアニメと同じ時計で消えないと、ラッシュ中に尾がプレイヤーの動きより長く残る
        const float lifeDt = trail.animControlled ? ownerDt : selfDt;

        // 描画側が、UV スクロールを老化と同じ倍率で遅くするために読む
        trail.timeScale = (dt > 0.0f) ? (lifeDt / dt) : 0.0f;

        // ---------------------------------------------------------
        // 2. 一時トレイル
        // ---------------------------------------------------------

        // 持ち主の時計で進める。切れると emitting が落ちるので、印を立てる前に進める
        // 世界の時計にすると、ラッシュ中（世界だけスロー）にダッシュの尾が何秒も残る
        trail.UpdateBurstTimer(ownerDt);

        // ---------------------------------------------------------
        // 3. 老化
        // ---------------------------------------------------------

        // 断面を古くし、寿命が切れたものを古い順に捨てる
        // 描画側でやると、尾の長さがリフレッシュレートで変わる
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

        // スロー中も毎ステップ立てる。実際に刻むかは TrailSystem が先端の移動量で決める
        // トレイルの時計に合わせて間引くと、持ち主だけ実時間で動くときに尾が粗い折れ線になる
        if (trail.emitting) trail.sampleRequested = true;
    });
}



float TrailRecordSystem::ClockDt(EntityID id, float fallbackDt) const
{
    if (id == InvalidEntityID) return fallbackDt;

    const auto *time = m_world->GetComponent<TimeStateComponent>(id);
    return time ? time->localDt : fallbackDt;
}



float TrailRecordSystem::WorldDt(float dt) const
{
    if (!m_world->HasResource<TimeContext>()) return dt;

    const auto &ctx = m_world->GetResource<TimeContext>();
    return ctx.isPaused ? 0.0f : dt * ctx.globalScale;
}



// emitting と ownerEntity を決める TrailAnimationSyncSystem（L05）の後
REGISTER_LOGIC_SYSTEM(TrailRecordSystem, Priority::LogicStage::L06_Resolution);
