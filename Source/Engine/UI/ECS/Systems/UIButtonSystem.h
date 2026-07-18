#pragma once
/// @file UIButtonSystem.h
/// @brief UIInteractable の状態を見た目(UIImage.color)へ反映し、クリックでイベント発火するシステム。
///
/// 位置付け:
///   - RenderStage::R10_UI。UIInputSystem が更新した状態を読む（同ステージのため色反映は最大1F遅延だが体感無問題）。
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/UI/ECS/Components/UIInteractableComponent.h"
#include "Engine/UI/ECS/Components/UIButtonComponent.h"
#include "Engine/UI/ECS/Components/UIImageComponent.h"

class UIButtonSystem : public CCL::ECS::IfSystem<UIButtonSystem,
                           CCL::ECS::Read<UIInteractableComponent>,
                           CCL::ECS::Read<UIButtonComponent>,
                           CCL::ECS::Write<UIImageComponent>>
{
  public:
    UIButtonSystem()
        : IfSystem("UIButtonSystem")
    {
    }

    void Update(float dt) override;
};
