/// @file UIInputSystem.cpp
/// 役割・設計方針は UIInputSystem.h を参照。
#include "UIInputSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/UI/ECS/UILayoutUtil.h"
#include "Engine/Platform/Input/Input.h"

using namespace CCL::ECS;

void UIInputSystem::Update(float /*dt*/)
{
    // マウス（具象 Input はリソース登録済み。無ければ何もしない）。
    if (!_world->HasResource<Input *>()) return;
    Input *input = _world->GetResource<Input *>();
    if (!input) return;

    Mouse      &mouse   = input->GetMouse();
    const float mx      = static_cast<float>(mouse.GetPositionX());
    const float my      = static_cast<float>(mouse.GetPositionY());
    const bool  pressed = mouse.IsTriggered(Mouse::BTN_LEFT); // 押した瞬間
    const bool  released = mouse.IsReleased(Mouse::BTN_LEFT); // 離した瞬間

    auto       &gfx     = Graphics::Instance();
    const float screenW = gfx.GetScreenWidth();
    const float screenH = gfx.GetScreenHeight();

    auto inRect = [&](const UITransformComponent &t) {
        return mx >= t.computedPos.x && mx <= t.computedPos.x + t.computedSize.x &&
               my >= t.computedPos.y && my <= t.computedPos.y + t.computedSize.y;
    };

    // --- Pass 1: 最前面(priority最大)のヒット要素を特定 ---
    EntityID topID       = InvalidEntityID;
    int      topPriority = 0;
    ForEachWithID([&](EntityID id, UITransformComponent &t, UIInteractableComponent &inter) {
        CCL::UI::ComputeUIRect(t, screenW, screenH);
        if (!inter.interactable) return;
        if (!inRect(t)) return;
        if (topID == InvalidEntityID || inter.priority >= topPriority)
        {
            topID       = id;
            topPriority = inter.priority;
        }
    });

    // --- Pass 2: 状態更新（hover/press/click） ---
    ForEachWithID([&](EntityID id, UITransformComponent & /*t*/, UIInteractableComponent &inter) {
        inter.clicked = false;

        const bool hot   = (id == topID) && inter.interactable;
        inter.isHovered  = hot;

        if (pressed && hot)
            inter.isPressed = true; // この要素で押下開始

        if (released)
        {
            if (inter.isPressed && hot)
                inter.clicked = true; // 押下した要素の上で離した＝クリック成立
            inter.isPressed = false;  // 離したら押下解除（要素外で離した場合も含む）
        }
    });
}

REGISTER_RENDER_SYSTEM(UIInputSystem, Priority::RenderStage::R10_UI);
