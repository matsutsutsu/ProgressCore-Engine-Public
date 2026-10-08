#pragma once
// ===========================================================================
// 概要: 表示する UIImageComponent を UIDrawList へ積む収集 System（R10b_UICollect）。
//
// 責務:
//   - UILayoutSystem が決めた computedPos / computedSize で、画像（または単色矩形）のアイテムを積む
//
// 備考:
//   - 矩形は計算しない（座標を決めるのは UILayoutSystem だけ）。描画は 2D パスの UIRenderer。
//   - 同じ帯の UITextCollectSystem / UIProgressBarCollectSystem とは直列に走らせる必要がある。
//     3 つともコンポーネントは Read だけなのでスケジューラは並列にしてよいと判断するが、
//     実際は同じ UIDrawList（World リソース）に push_back するので同時に走ると壊れる。
//     スケジューラはリソースの共有を見ないため、GetRunAfter で
//     Image → Text → ProgressBar の順に並べている。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

class UIImageCollectSystem : public CCL::ECS::IfSystem<UIImageCollectSystem,
                                                       CCL::ECS::Read<UITransformComponent>,
                                                       CCL::ECS::Read<UIImageComponent>>
{
public:
    UIImageCollectSystem()
        : IfSystem("UIImageCollectSystem")
    {
    }

    void Update(float dt) override;
};
