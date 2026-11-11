#pragma once
// ===========================================================================
// 概要: UIInteractable の状態をボタンの見た目に反映し、クリックを通知する System（R10_UI）
//
// 責務:
//   - 状態に応じて UIImage.color を差し替え、UIAnimationComponent の目標スケールを書く
//   - クリックされたら UIButtonClickedEvent を発行する
//
// 備考: UIInputSystem の後に走るので、入力と同じフレームで見た目が変わる
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

class UIButtonSystem : public CCL::ECS::IfSystem<
    UIButtonSystem,
    CCL::ECS::Read<UIInteractableComponent>,
    CCL::ECS::Read<UIButtonComponent>,
    CCL::ECS::Write<UIImageComponent>>
{
public:
    UIButtonSystem() : IfSystem("UIButtonSystem") {}

    // UIInputSystem が書いた UIInteractableComponent の状態を、同じフレームのうちに読む
    std::vector<const char *> GetRunAfter() const override { return {"UIInputSystem"}; }

    void Update(float dt) override;
};
