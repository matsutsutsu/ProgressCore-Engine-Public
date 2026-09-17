#pragma once
// 可視な UITextComponent を UIDrawList へ積む収集システム。
//
// 役割:
//   - UILayoutSystem が確定した computedPos を読み、テキスト描画アイテムを積む（矩形計算はしない）。
//   - 実描画は2DパスのUIRenderer→TextRenderer が行う（ここでは積むだけ）。
// 位置付け:
//   - RenderStage::R10b_UICollect。UIの状態が全部決まった後の帯。
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

    // 同じ UIDrawList へ積む収集System同士は直列でなければならない。
    // 全員 Read のみなのでスケジューラは並列可と判断してしまう（リソースの共有は見えない）。
    // 詳細は UIImageCollectSystem.h を参照。
    std::vector<const char *> GetRunAfter() const override { return {"UIImageCollectSystem"}; }

    void Update(float dt) override;
};
