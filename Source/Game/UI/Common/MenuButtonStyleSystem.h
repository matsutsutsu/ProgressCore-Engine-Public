#pragma once
// ===========================================================================
// 概要: メニューのボタンの見た目（選択の帯・ラベルの色・押したときの演出）を毎フレーム動かす System。
//
// 責務:
//   - 選択（focus / hover）されたボタンの帯を左から伸ばし、外れたら縮める
//   - 帯の伸び具合に合わせて、ラベルを白から暗い色へ変える
//   - 押された瞬間に、帯を白く飛ばしてグリッチを走らせる
//
// 備考: 対象は MenuUiFactory::SpawnButton が作ったボタン（MenuButtonVisualComponent を持つもの）。
//       帯は HP ゲージと同じ UIProgressBar（UIGaugePS）で描くので、傾き・光沢・先端の光・グリッチを使い回せる。
//       描画ステージで走るのは、ポーズ中（ゲームの時間が止まっている間）もメニューは動かす必要があるため。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIProgressBarComponent.h"
#include "Game/UI/Common/MenuButtonVisualComponent.h"

class MenuButtonStyleSystem : public CCL::ECS::IfSystem<MenuButtonStyleSystem,
                                                        CCL::ECS::Read<UIInteractableComponent>,
                                                        CCL::ECS::Write<MenuButtonVisualComponent>,
                                                        CCL::ECS::Write<UIProgressBarComponent>>
{
public:
    MenuButtonStyleSystem()
        : IfSystem("MenuButtonStyleSystem")
    {
    }

    // UIInputSystem が決めた focus / hover / clicked を同じフレームで読むため、その後に走らせる
    std::vector<const char *> GetRunAfter() const override
    {
        return {"UIInputSystem"};
    }

    void Update(float dt) override;

private:
    // --- 調整値 ---
    static constexpr float kFocusSpeed = 16.0f; // 帯が伸び縮みする速さ（大きいほど速い。16 で約 0.2 秒）
    static constexpr float kPressTime  = 0.35f; // 押したときの白飛びと揺れが収まるまで（秒）
};
