#pragma once
// ===========================================================================
// 概要: UITransform.scale を UIAnimationComponent.targetScale へ追従させる System（R10_UI）。
//
// 責務:
//   - 指数減衰で scale を目標へ近づける（描画フレームの dt を使うのでフレームレートに依存しない）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"

class UIAnimationSystem : public CCL::ECS::IfSystem<UIAnimationSystem,
                                                    CCL::ECS::Write<UITransformComponent>,
                                                    CCL::ECS::Read<UIAnimationComponent>>
{
public:
    UIAnimationSystem()
        : IfSystem("UIAnimationSystem")
    {
    }

    // UIButtonSystem が hover/focus を見て targetScale を書いた後に追従させる。
    //   逆順だと、押した瞬間の拡大が次フレームまで反映されない。
    //   UIButtonSystem は UIAnimationComponent を GetComponent で書くので、型の競合として
    //   スケジューラに見えない。そのため順序をここで明示する
    std::vector<const char *> GetRunAfter() const override { return {"UIButtonSystem"}; }

    void Update(float dt) override;
};
