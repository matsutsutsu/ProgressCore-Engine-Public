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

    void Update(float dt) override;
};
