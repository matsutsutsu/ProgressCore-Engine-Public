#pragma once
// ===========================================================================
// 概要: 全ての UITransformComponent の矩形を画面上のピクセルに確定させる System（R10_UI）。
//
// 責務:
//   - anchor / pivot / 親子 / Canvas を解決し、computedPos / computedSize を書き込む
//   - UI 描画リスト（UIDrawList）のリソースを用意する
//
// 備考:
//   - UI の座標を決めるのはここだけ。収集 System（画像・文字・ゲージ）も入力判定も、
//     自分で計算せずこの結果を読む。計算を各所に持つと、直したときに 1 か所だけ古いまま残るため。
//   - UITransformComponent だけを条件にしているので、画像も文字も持たない入れ物
//     （Canvas や、まとめて動かすためのグループ）の矩形も確定する。
//   - アニメ・出現演出が UITransform を動かし終えた後に走る。
//     収集は次の帯（R10b_UICollect）なので、前後はステージの順で保証される。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"

class UILayoutSystem : public CCL::ECS::IfSystem<UILayoutSystem,
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
