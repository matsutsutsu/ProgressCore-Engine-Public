#pragma once
// すべての UITransformComponent の矩形を画面ピクセルへ確定させるシステム。
//
// 役割:
//   - アンカー/ピボット/親子/Canvas を解決し、computedPos/computedSize を書き込む（解像度非依存）。
//   - 「UIの座標を決める場所」はここ1箇所だけ。収集System(画像/文字/ゲージ)も入力判定も、
//     自分で計算せずこの結果を読む。
//
// なぜ収集(描画リスト構築)と分けたか:
//   以前はこのSystemが「画像の矩形計算＋画像の収集」を兼ねており、文字・ゲージ・入力判定の
//   各Systemがそれぞれ同じ計算をやり直していた（同じ処理が4箇所）。計算の置き場が散ると、
//   直したつもりが1箇所だけ古いままになる。矩形はここで全部決め、他は読むだけにする。
//
// 対象:
//   UITransformComponent だけを宣言している＝UIImage も UIText も持たない「入れ物」
//   （Canvas や、まとめて動かすためのグループノード）も漏れなく確定される。
//
// 位置付け:
//   - RenderStage::R10_UI。アニメ・出現演出が UITransform を動かし終えた後に走る。
//   - 収集は次の帯(R10b_UICollect)。ステージが違うので前後は自動的に保証される。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UITransformComponent.h"

class UILayoutSystem : public CCL::ECS::IfSystem<UILayoutSystem,
                                                 CCL::ECS::Write<UITransformComponent>>
{
public:
    UILayoutSystem()
        : IfSystem("UILayoutSystem")
    {
    }

    void Initialize() override;

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // アニメ・出現演出が UITransform を動かし終えてから画面ピクセルへ確定させる。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UIAnimationSystem", "UIAppearSystem"};
    }

    void Update(float dt) override;
};
