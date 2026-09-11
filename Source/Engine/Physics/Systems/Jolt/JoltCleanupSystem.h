#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"

// DestroyTagがついたEntityのJoltの剛体を削除するシステム
//
// HierarchyCleanupSystemとの順番は実はどっちでもいい
//   実際に消されたとしても遅延実行なので問題なく剛体削除処理は通るので
// 、HierarchyCleanupSystemが先に走っても問題ない
class JoltCleanupSystem : public CCL::ECS::IfSystem<JoltCleanupSystem,
    CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Read<JoltHandleComponent>> {
public:
    JoltCleanupSystem() : IfSystem("JoltCleanupSystem") {}
    void Update(float dt) override;

    // 外部から強制的にクリーンアップを走らせる静的関数
    static void ExecuteCleanup(CCL::ECS::World& world);
};

