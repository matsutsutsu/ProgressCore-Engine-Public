#pragma once
// 可視な UIImageComponent を UIDrawList へ積む収集システム（画像 or 単色矩形）。
//
// 役割:
//   - UILayoutSystem が確定した computedPos/computedSize を読み、描画アイテムを1件積む。
//   - 矩形計算は一切しない（座標を決めるのは UILayoutSystem だけ）。実描画は2DパスのUIRenderer。
//
// 位置付け:
//   - RenderStage::R10b_UICollect。UIの状態（色・スケール・矩形）が全部決まった後の帯。
//   - 同じ帯の UITextCollectSystem / UIProgressBarCollectSystem とは直列に走らせる必要がある。
//     3つとも読み取り専用なのでスケジューラは「並列にしてよい」と判断するが、実際には
//     同じ UIDrawList（Worldリソース）へ push_back するので同時に走らせると壊れる。
//     スケジューラが見ているのはコンポーネントの Read/Write だけで、リソースの共有は
//     見えない。そのため GetRunAfter で明示的に数珠つなぎにしている。
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
