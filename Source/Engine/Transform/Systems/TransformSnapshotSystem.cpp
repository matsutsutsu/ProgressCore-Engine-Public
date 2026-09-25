#include "TransformSnapshotSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// L01_Input と同じステージ。PlayerInputSystem 等は TransformComponent に書かないので、
// 型が競合せず TaskScheduler が並列に実行できる
REGISTER_LOGIC_SYSTEM(TransformSnapshotSystem, Priority::LogicStage::L01_Input);

void TransformSnapshotSystem::Update(float dt)
{
    ForEachParallel([](TransformComponent &trans) {
        // 静的物体は動かないので退避しない（補間側も静的物体は prev を読まない）
        if (trans.isStatic) return;

        trans.prevPosition = trans.position;
        trans.prevRotation = trans.rotation;
    });
}
