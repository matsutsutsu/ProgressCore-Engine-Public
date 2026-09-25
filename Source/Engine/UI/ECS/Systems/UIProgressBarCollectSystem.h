#pragma once
// ===========================================================================
// 概要: UIProgressBar を「背景 +（遅延バー）+ フィル」の矩形として UIDrawList へ積む収集 System（R10b_UICollect）。
//
// 責務:
//   - UILayoutSystem が決めた computed* から各矩形を作り、被弾の揺れのオフセットを足して積む
//
// 備考: 矩形は計算しない。描画は 2D パスの UIRenderer。
// ===========================================================================
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

    // 同じ UIDrawList へ積む収集 System 同士は直列でなければならない（詳細は UIImageCollectSystem.h）
    std::vector<const char *> GetRunAfter() const override { return {"UITextCollectSystem"}; }

    void Update(float dt) override;
};
