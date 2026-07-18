#pragma once
/// @file UIProgressBarCollectSystem.h
/// @brief UIProgressBar を「背景＋フィル」の2矩形として UIDrawList へ積む収集システム。
/// 位置付け: RenderStage::R10_UI（UpdateRender中）。描画は2DパスのUIRenderer。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"

class UIProgressBarCollectSystem : public CCL::ECS::IfSystem<UIProgressBarCollectSystem,
                                       CCL::ECS::Write<UITransformComponent>,
                                       CCL::ECS::Read<UIProgressBarComponent>>
{
  public:
    UIProgressBarCollectSystem()
        : IfSystem("UIProgressBarCollectSystem")
    {
    }

    void Update(float dt) override;
};
