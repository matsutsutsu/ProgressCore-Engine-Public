#pragma once
// ===========================================================================
// 概要: このステップで消える Entity（DestroyTag の起点とその子孫）の Jolt 剛体を破棄する System。
//
// 責務:
//   - HierarchyCleanupSystem と同じ関数で「消える Entity」を集め、その剛体を Remove / Destroy する
//   - 破棄した JoltHandleComponent の bodyID を無効値に戻す（二重破棄を防ぐ）
//
// 備考: HierarchyCleanupSystem より先に走る（向こうが RunAfter で宣言している）。
//       ECS の構造を壊す前に外部リソースを解放するため。bodyID を書き戻すので申告は Write。
//       シーン切り替え時は BaseScene / SceneManager が ExecuteCleanup を直接呼ぶ。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Core/Components/DestroyTag.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
class JoltCleanupSystem : public CCL::ECS::IfSystem<JoltCleanupSystem,
                                                    CCL::ECS::Read<Tag::DestroyTag>, CCL::ECS::Write<JoltHandleComponent>>
{
public:
    JoltCleanupSystem() : IfSystem("JoltCleanupSystem") {}
    void Update(float dt) override;

    // System の Update を待たずにクリーンアップを走らせる（シーン破棄時用）
    static void ExecuteCleanup(CCL::ECS::World &world);
};
