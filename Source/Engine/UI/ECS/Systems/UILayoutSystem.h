#pragma once
// ===========================================================================
// 概要: すべての UITransformComponent の矩形を画面上のピクセルに確定させる System（R10_UI）
//
// 責務:
//   - anchor / pivot / 親子 / Canvas を解決し、computedPos / computedSize を書き込む
//   - UI 描画リスト（UIDrawList）のリソースを用意する
//
// 備考: UI の座標を決めるのはここだけ。収集の System も入力の判定も、この結果を読む
//       UITransformComponent だけを条件にするので、画像も文字も持たない入れ物（Canvas やグループ）の矩形も決まる
//       アニメと出現の演出が UITransform を動かした後に走り、収集は次の帯（R10b_UICollect）で走る
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"

class UILayoutSystem : public CCL::ECS::IfSystem<
    UILayoutSystem,
    CCL::ECS::Write<UITransformComponent>>
{
public:
    UILayoutSystem() : IfSystem("UILayoutSystem") {}

    void Initialize() override;

    // アニメ・出現演出が UITransform を動かし終えてから矩形を確定させる
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UIAnimationSystem", "UIAppearSystem"};
    }

    void Update(float dt) override;
};
