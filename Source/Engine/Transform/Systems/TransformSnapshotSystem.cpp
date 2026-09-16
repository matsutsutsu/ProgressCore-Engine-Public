
// TransformSnapshotSystem.cpp
#include "TransformSnapshotSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// L01_Input と同優先度：TransformComponentへの書き込みはPlayerInputSystem等と
// 型競合しないため、TaskSchedulerが安全に並列実行してくれる
REGISTER_LOGIC_SYSTEM(TransformSnapshotSystem, Priority::LogicStage::L01_Input);

void TransformSnapshotSystem::Update(float dt)
{
    ForEachParallel([](TransformComponent &trans) {
        // 静的オブジェクトは動かないため退避コスト自体が無駄
        if (trans.isStatic) return;

        trans.prevPosition = trans.position;
        trans.prevRotation = trans.rotation;
    });
}
