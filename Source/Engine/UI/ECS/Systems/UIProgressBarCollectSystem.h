#pragma once
// UIProgressBar を「背景＋（遅延バー）＋フィル」の矩形として UIDrawList へ積む収集システム。
// 矩形計算はしない（UILayoutSystem が確定した computed* を読むだけ）。
// 位置付け: RenderStage::R10b_UICollect。描画は2DパスのUIRenderer。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"

class UIProgressBarCollectSystem : public CCL::ECS::IfSystem<UIProgressBarCollectSystem,
                                                             CCL::ECS::Read<UITransformComponent>,
                                                             CCL::ECS::Read<UIProgressBarComponent>>
{
public:
    UIProgressBarCollectSystem()
        : IfSystem("UIProgressBarCollectSystem")
    {
    }

    // 同じ UIDrawList へ積む収集System同士は直列でなければならない。
    // 詳細は UIImageCollectSystem.h を参照。
    std::vector<const char *> GetRunAfter() const override { return {"UITextCollectSystem"}; }

    void Update(float dt) override;
};
