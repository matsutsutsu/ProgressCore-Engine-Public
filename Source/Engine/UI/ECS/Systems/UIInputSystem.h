#pragma once
// ===========================================================================
// 概要: マウス・パッド・キーの入力で UIInteractable の状態を更新する System（R10_UI）
//
// 責務:
//   - マウス: 最前面（priority 最大）の要素だけを hover にし、同じ要素で押して離したら clicked にする
//   - パッド/キー: 方向入力でフォーカスを隣の要素へ移し、決定ボタンで clicked にする
//   - マウス座標を GameViewportInfo でゲーム RT の座標へ変換する
//
// 備考: clicked のリセットと確定をこの System に集め、複数の System が書いて競合しないようにしている
//       ヒット判定には前フレームの computed* を使う。矩形は scale に、scale は hover の状態に依存して循環するので、
//       どこか1か所を前フレームの値にするしかなく、影響が一番小さい判定の矩形を選んだ（ずれは hover 拡大の数 px）
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

class UIInputSystem : public CCL::ECS::IfSystem<
    UIInputSystem,
    CCL::ECS::Read<UITransformComponent>,
    CCL::ECS::Write<UIInteractableComponent>>
{
public:
    UIInputSystem() : IfSystem("UIInputSystem") {}

    // UITransform を書く3つの System より前に走らせる
    // こちらは computed* を読むだけで実際の依存はないが、宣言しないと順序不定の警告が出る
    std::vector<const char *> GetRunBefore() const override
    {
        return {"UIAnimationSystem", "UIAppearSystem", "UILayoutSystem"};
    }

    void Update(float dt) override;
};
