#pragma once
// ===========================================================================
// 概要: 表示する UIImageComponent を UIDrawList へ積む収集 System（R10b_UICollect）
//
// 責務:
//   - UILayoutSystem が決めた computedPos / computedSize で、画像（または単色矩形）のアイテムを積む
//
// 備考: 矩形は計算しない（座標を決めるのは UILayoutSystem だけ）。描くのは 2D パスの UIRenderer
//       同じ帯の UITextCollectSystem / UIProgressBarCollectSystem とは同じ UIDrawList に積むので、直列に走らせる
//       スケジューラはリソースの共有を見ないので、GetRunAfter で Image、Text、ProgressBar の順に並べている
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

class UIImageCollectSystem : public CCL::ECS::IfSystem<
    UIImageCollectSystem,
    CCL::ECS::Read<UITransformComponent>,
    CCL::ECS::Read<UIImageComponent>>
{
public:
    UIImageCollectSystem() : IfSystem("UIImageCollectSystem") {}

    void Update(float dt) override;
};
