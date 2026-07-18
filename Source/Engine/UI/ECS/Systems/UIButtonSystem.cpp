/// @file UIButtonSystem.cpp
/// 役割・設計方針は UIButtonSystem.h を参照。
#include "UIButtonSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Platform/Core/Logger.h"

#include "Engine/UI/ECS/UIEvents.h"

using namespace CCL::ECS;

void UIButtonSystem::Update(float /*dt*/)
{
    ForEachWithID([&](EntityID id, const UIInteractableComponent &inter,
                      const UIButtonComponent &btn, UIImageComponent &img) {
        // 状態に応じた色へ差し替え（Disabled > Pressed > Hover > Normal の優先）
        if (!inter.interactable)
            img.color = btn.disabledColor;
        else if (inter.isPressed)
            img.color = btn.pressedColor;
        else if (inter.isHovered)
            img.color = btn.hoverColor;
        else
            img.color = btn.normalColor;

        // クリック成立でイベント発火（ゲーム側がハッシュで分岐）
        if (inter.clicked)
        {
            const uint32_t hash = btn.onClickAction.empty()
                                      ? 0u
                                      : CCL::Utils::HashString(btn.onClickAction);
            _world->GetEventBus().Publish(UIButtonClickedEvent{id, hash});

            CCL_LOG_INFO(LogCategory::Game, "[UIButton] clicked entity=%llu action='%s'",
                static_cast<unsigned long long>(id), btn.onClickAction.c_str());
        }
    });
}

REGISTER_RENDER_SYSTEM(UIButtonSystem, Priority::RenderStage::R10_UI);
