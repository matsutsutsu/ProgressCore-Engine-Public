#pragma once
// UITransform.scale を UIAnimationComponent.targetScale へ指数減衰で追従させる。
// 位置付け: RenderStage::R10_UI（UpdateRender中・毎フレーム1回、dtで補間）。
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
    //   （Button は UIAnimationComponent を GetComponent で触るため型競合としては現れない。
    //     明示宣言が無いとスケジューラからは独立に見えるので、ここで順序を固定する）
    std::vector<const char *> GetRunAfter() const override { return {"UIButtonSystem"}; }

    void Update(float dt) override;
};
