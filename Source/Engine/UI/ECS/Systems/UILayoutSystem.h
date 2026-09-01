#pragma once
// アンカー方式のUI矩形を画面ピクセルへ確定し、描画リスト(UIDrawList)を構築するシステム。
//
// 役割:
//   - 画面サイズから各 UITransformComponent の computedPos/Size を計算（解像度非依存）。
//   - 可視な UIImageComponent を UIDrawList へ積む（実描画は2DパスのUIRendererが行う）。
// 位置付け:
//   - RenderStage::R10_UI（UpdateRender中）で走る。直接描画はしない（積むだけ）。
//   ※ Slice 1 は「UITransform + UIImage」を対象。テキスト等は後続スライスで収集を追加する。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

class UILayoutSystem : public CCL::ECS::IfSystem<UILayoutSystem,
                           CCL::ECS::Write<UITransformComponent>,
                           CCL::ECS::Read<UIImageComponent>>
{
  public:
    UILayoutSystem()
        : IfSystem("UILayoutSystem")
    {
    }

    void Initialize() override;

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // アニメ・出現演出が UITransform を動かし終えてから画面ピクセルへ確定させる。
    std::vector<const char *> GetRunAfter() const override { return {"UIAnimationSystem", "UIAppearSystem"}; }

    void Update(float dt) override;
};
