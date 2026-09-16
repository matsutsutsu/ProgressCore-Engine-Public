#pragma once
// マウス座標で UI 要素をヒットテストし、hover/press/click 状態を更新するシステム。
//
// 役割:
//   - UITransform のアンカー矩形を計算し、最前面(priority最大)の要素だけを hover 対象にする。
//   - 左ボタンの押下→同要素で離す、で clicked を成立させる。
// 位置付け:
//   - RenderStage::R10_UI（毎フレーム1回の UpdateRender で走る）。マウスのエッジ判定は
//     プラットフォーム Input が毎フレーム更新するため、ここで読めば取りこぼさない。
//   - 座標は現状ウィンドウクライアント基準（エディタのGameViewビュー内オフセットは未対応）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

class UIInputSystem : public CCL::ECS::IfSystem<UIInputSystem,
                                                CCL::ECS::Write<UITransformComponent>,
                                                CCL::ECS::Write<UIInteractableComponent>>
{
public:
    UIInputSystem()
        : IfSystem("UIInputSystem")
    {
    }

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // UILayout が確定した computedPos/computedSize に対してヒット判定する。
    std::vector<const char *> GetRunAfter() const override { return {"UILayoutSystem"}; }

    void Update(float dt) override;
};
