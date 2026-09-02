// ===========================================================================
// TrailRecordSystem.cpp
// 役割・責務・位置付けは TrailRecordSystem.h を参照。
// ===========================================================================
#include "TrailRecordSystem.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TrailRecordSystem::Update(float dt)
{
    ForEach([dt](TrailComponent &trail) {

        // 一時表示タイマー。切れると emitting が落ちるので、印の発行より前に更新する。
        trail.UpdateBurstTimer(dt);

        // 既存区間を老化させ、寿命切れを最古側から破棄する。
        //   ここが描画相にあると、尾の長さが環境のリフレッシュレートで変わってしまう。
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

        // 描画相へ「このステップぶんを1区間刻んでよい」と伝える。
        //   立てるだけで、実際に刻むかどうかは移動量ゲート(minDistance)が決める。
        if (trail.emitting)
            trail.sampleRequested = true;
    });
}

// ロジック相で尾の時間を進める。emitting を決める TrailAnimationSyncSystem(L05)より後。
REGISTER_LOGIC_SYSTEM(TrailRecordSystem, Priority::LogicStage::L06_Resolution);
