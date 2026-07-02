// --- JoltCleanupSystem.h ---
#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"

class JoltCleanupSystem : public CCL::ECS::IfSystem<JoltCleanupSystem,
    CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Read<JoltHandleComponent>> {
public:
    JoltCleanupSystem() : IfSystem("JoltCleanupSystem") {}
    void Update(float dt) override;

    // 外部から強制的にクリーンアップを走らせる静的関数
    static void ExecuteCleanup(CCL::ECS::Core::World& world);
};

