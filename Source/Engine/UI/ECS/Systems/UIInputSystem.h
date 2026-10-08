#pragma once
// ===========================================================================
// 概要: マウス・パッド・キーの入力で UIInteractable の状態を更新する System（R10_UI）。
//
// 責務:
//   - マウス: 最前面（priority 最大）の要素だけを hover にし、同じ要素で押して離したら clicked にする
//   - パッド/キー: 方向入力でフォーカスを隣の要素へ移し、決定ボタンで clicked にする
//   - マウス座標を GameViewportInfo でゲーム RT の座標へ変換する
//
// 備考:
//   - clicked のリセットと確定をこの System 1 つに集め、複数の System が書いて競合しないようにしている。
//   - R10_UI（描画フレームごとに 1 回）で走る。マウスの押した/離した判定は
//     プラットフォームの Input が毎フレーム更新するので、ここで読めば取りこぼさない。
//   - ヒット判定には前フレームの computed* を使う。UI の更新は
//     入力 → ボタン状態 → 追従アニメ（scale）→ 矩形確定 → 収集 の順に流しているが、
//     矩形は scale に、scale はボタンの状態（hover で拡大）に依存していて循環している。
//     どこか 1 か所を前フレームの値にするしかなく、影響が一番小さいヒット判定の矩形を選んだ。
//     フレーム間で変わるのは hover 拡大の数 px だけで、判定がずれても体感できない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"

class UIInputSystem : public CCL::ECS::IfSystem<UIInputSystem,
                                                CCL::ECS::Read<UITransformComponent>,
                                                CCL::ECS::Write<UIInteractableComponent>>
{
public:
    UIInputSystem()
        : IfSystem("UIInputSystem")
    {
    }

    /**
     * @brief UITransform を書く 3 つの System より前に走らせる。
     *
     * こちらは computed* を読むだけなので型としては競合するが、実際の依存は無い。
     * 宣言しないと「順序不定」の警告が出て、どちらが先かが登録順で決まってしまう。
     */
    std::vector<const char *> GetRunBefore() const override
    {
        return {"UIAnimationSystem", "UIAppearSystem", "UILayoutSystem"};
    }

    void Update(float dt) override;
};
