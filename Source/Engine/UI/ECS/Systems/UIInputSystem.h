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
//
// ヒット判定に使う矩形は「前フレームの computed*」である点に注意:
//   UIの更新は 入力 → ボタン状態 → 追従アニメ(scale) → 矩形確定 → 収集 の一方向に流している。
//   ところが矩形は scale に依存し、その scale はボタンの状態（hoverで拡大）に依存するので、
//   厳密には循環している。どこか1箇所を前フレームに倒すしかなく、影響が一番小さい
//   「ヒット判定の矩形」を選んだ。フレーム間で変わるのは hover 拡大の数pxだけで、
//   判定がずれても体感できない。逆に色やスケールを遅らせると押した手応えが鈍って分かる。
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

    // UITransform を書き換える3システムより前に走ることを宣言する。
    //   こちらは computed* を読むだけなので型としては競合するが、実依存は無い。
    //   宣言しないと「順序不定」として警告され、どちらが先かが並び順まかせになる。
    std::vector<const char *> GetRunBefore() const override
    {
        return {"UIAnimationSystem", "UIAppearSystem", "UILayoutSystem"};
    }

    void Update(float dt) override;
};
