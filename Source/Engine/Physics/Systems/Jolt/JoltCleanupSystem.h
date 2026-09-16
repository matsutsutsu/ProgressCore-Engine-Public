#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"

// このフレームで消えるEntity（DestroyTagの起点＋その子孫）のJolt剛体を削除するシステム
//
// HierarchyCleanupSystem より先に走る必要がある（向こうが RunAfter で宣言している）。
//   ECSの構造を壊す前に外部リソースを解放する、という順序を明示しておく。
//   bodyID は破棄の証拠として書き戻すので、申告は Write。
class JoltCleanupSystem : public CCL::ECS::IfSystem<JoltCleanupSystem,
                                                    CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Write<JoltHandleComponent>>
{
public:
    JoltCleanupSystem() : IfSystem("JoltCleanupSystem") {}
    void Update(float dt) override;

    // 外部から強制的にクリーンアップを走らせる静的関数
    static void ExecuteCleanup(CCL::ECS::World &world);
};
