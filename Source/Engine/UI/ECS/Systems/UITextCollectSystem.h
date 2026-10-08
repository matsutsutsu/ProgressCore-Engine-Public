#pragma once
// ===========================================================================
// 概要: 表示する UITextComponent を UIDrawList へ積む収集 System（R10b_UICollect）。
//
// 責務:
//   - UILayoutSystem が決めた computedPos で、文字のアイテムを積む
//
// 備考: 矩形は計算しない。描画は 2D パスの UIRenderer → TextRenderer。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

class UITextCollectSystem : public CCL::ECS::IfSystem<UITextCollectSystem,
                                                      CCL::ECS::Read<UITransformComponent>,
                                                      CCL::ECS::Read<UITextComponent>>
{
public:
    UITextCollectSystem()
        : IfSystem("UITextCollectSystem")
    {
    }

    // 同じ UIDrawList へ積む収集 System 同士は直列でなければならない（詳細は UIImageCollectSystem.h）
    std::vector<const char *> GetRunAfter() const override { return {"UIImageCollectSystem"}; }

    void Update(float dt) override;
};
