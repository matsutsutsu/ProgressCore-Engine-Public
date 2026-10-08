#include "TrailRecordSystem.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TrailRecordSystem::Update(float dt)
{
    ForEach([dt](TrailComponent &trail) {
        // 一時トレイルのタイマー。切れると emitting が落ちるので、印を立てる前に進める
        trail.UpdateBurstTimer(dt);

        // 断面の age を進め、寿命を超えたものを古い順に捨てる。
        //   これを描画側で行うと、尾の長さがリフレッシュレートで変わってしまう。
        const int MAX = TrailComponent::MAX_SEGMENTS;

        for (int i = 0; i < trail.count; ++i)
        {
            const int idx = (trail.head + i) % MAX;
            trail.segments[idx].age += dt;
        }

        while (trail.count > 0 && trail.segments[trail.head].age >= trail.lifeTime)
        {
            trail.head = (trail.head + 1) % MAX;
            trail.count--;
        }

        // 描画側へ「このステップの分を1区間刻んでよい」と伝える。
        //   実際に刻むかどうかは、TrailSystem が先端の移動量（minDistance）で決める。
        if (trail.emitting)
            trail.sampleRequested = true;
    });
}

// emitting を決める TrailAnimationSyncSystem（L05）の後
REGISTER_LOGIC_SYSTEM(TrailRecordSystem, Priority::LogicStage::L06_Resolution);
