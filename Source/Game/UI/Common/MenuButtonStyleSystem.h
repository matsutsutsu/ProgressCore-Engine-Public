#pragma once
// ===========================================================================
// 概要: メニューのボタンの見た目（選択の帯・ラベルの色・押したときの演出）を毎フレーム動かす System。
//
// 責務:
//   - 選ばれたボタンの帯を左から伸ばし、外れたら縮める
//   - 帯の伸び具合に合わせて、ラベルを白から暗い色へ変える
//   - 押された瞬間に、帯を白く飛ばしてグリッチを走らせる
//
// 備考: 見た目の値は MenuStyleComponent から毎フレーム読む。
//       ポーズ中もメニューは動かすので、描画ステージで動かす。
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
    MenuButtonStyleSystem() : IfSystem("MenuButtonStyleSystem") {}

    // UIInputSystem が決めた focus / hover / clicked を同じフレームで読むため、その後に動かす
    std::vector<const char *> GetRunAfter() const override { return {"UIInputSystem"}; }

    void Update(float dt) override;

private:
    CCL::ECS::EntityID m_styleID = CCL::ECS::InvalidEntityID; // MenuStyle（見つけたら持っておく）
};
