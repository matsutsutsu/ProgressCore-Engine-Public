// 役割・設計方針は UIButtonSystem.h を参照。
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
        // 状態に応じた色へ差し替え（Disabled > Pressed > Hover/Focus > Normal の優先）。
        // マウスhover と パッド/キーfocus は同じハイライト扱い。
        if (!inter.interactable)
            img.color = btn.disabledColor;
        else if (inter.isPressed)
            img.color = btn.pressedColor;
        else if (inter.isHovered || inter.isFocused)
            img.color = btn.hoverColor;
        else
            img.color = btn.normalColor;

        // hover/focus で拡大演出（UIAnimationComponent があれば目標スケールを設定）。
        if (auto *anim = m_world->GetComponent<UIAnimationComponent>(id))
        {
            const bool hot     = inter.interactable && (inter.isHovered || inter.isFocused);
            anim->targetScale  = hot ? btn.hoverScale : 1.0f;
        }

        // クリック成立でイベント発火（ゲーム側がハッシュで分岐）
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
