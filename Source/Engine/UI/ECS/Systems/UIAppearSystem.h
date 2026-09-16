#pragma once
// UIAppearComponent を駆動し、オーバーレイの出現アニメ（フェード＋上スライド）を再生する。
// 位置付け: RenderStage::R10_UI（UpdateRender中・毎フレーム1回、dtで進行）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIAppearComponent.h"

class UIAppearSystem : public CCL::ECS::IfSystem<UIAppearSystem,
                                                 CCL::ECS::Write<UITransformComponent>,
                                                 CCL::ECS::Write<UIAppearComponent>>
{
public:
    UIAppearSystem()
        : IfSystem("UIAppearSystem")
    {
    }

    void Update(float dt) override;
};
