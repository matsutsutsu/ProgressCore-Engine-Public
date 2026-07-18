#pragma once
/// @file UITextCollectSystem.h
/// @brief UITextComponent のレイアウト計算＋テキスト描画アイテムの収集システム。
///
/// 役割:
///   - UITransform のアンカーから画面px矩形を確定し、可視テキストを UIDrawList へ積む。
///   - 実描画は2DパスのUIRenderer→TextRenderer が行う（ここでは積むだけ）。
/// 位置付け:
///   - RenderStage::R10_UI（UpdateRender中）。UILayoutSystem(画像)と別集合を走査するため別System。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UITextComponent.h"

class UITextCollectSystem : public CCL::ECS::IfSystem<UITextCollectSystem,
                                CCL::ECS::Write<UITransformComponent>,
                                CCL::ECS::Read<UITextComponent>>
{
  public:
    UITextCollectSystem()
        : IfSystem("UITextCollectSystem")
    {
    }

    void Update(float dt) override;
};
