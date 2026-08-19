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


    /// @brief 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    ///        UILayout が確定した computedPos/computedSize を読んで描画リストへ積む。
    std::vector<const char *> GetRunAfter() const override { return {"UILayoutSystem"}; }

    void Update(float dt) override;
};
