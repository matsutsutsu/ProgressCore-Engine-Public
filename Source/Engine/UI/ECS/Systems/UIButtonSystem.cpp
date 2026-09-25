#include "UIButtonSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Core/Components/EntityLabel.h"

#include "Engine/UI/ECS/UIEvents.h"
#include "Engine/UI/ECS/Components/UIAnimationComponent.h"

using namespace CCL::ECS;

void UIButtonSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID id, const UIInteractableComponent &inter,
                      const UIButtonComponent &btn, UIImageComponent &img) {
        // 状態に応じた色へ差し替える（優先度は 無効 > 押下 > hover/focus > 通常）。
        // マウスの hover とパッド/キーの focus は同じハイライトにする
        if (!inter.interactable)
            img.color = btn.disabledColor;
        else if (inter.isPressed)
            img.color = btn.pressedColor;
        else if (inter.isHovered || inter.isFocused)
            img.color = btn.hoverColor;
        else
            img.color = btn.normalColor;

        // hover/focus で拡大する（UIAnimationComponent がある場合だけ）
        if (auto *anim = m_world->GetComponent<UIAnimationComponent>(id))
        {
            const bool hot    = inter.interactable && (inter.isHovered || inter.isFocused);
            anim->targetScale = hot ? btn.hoverScale : 1.0f;
        }

        // クリックされたらイベントを発行する（ゲーム側がハッシュで処理を分ける）
        if (inter.clicked)
        {
            const uint32_t hash = btn.onClickAction.empty()
                                      ? 0u
                                      : CCL::Utils::HashString(btn.onClickAction);
            m_world->GetEventBus().Publish(UIButtonClickedEvent{id, hash});

            CCL_LOG_INFO(LogCategory::Game, "[UIButton] clicked %s action='%s'",
                         CCL::EntityLabel(*m_world, id).c_str(), btn.onClickAction.c_str());
        }
    });
}

REGISTER_RENDER_SYSTEM(UIButtonSystem, Priority::RenderStage::R10_UI);
