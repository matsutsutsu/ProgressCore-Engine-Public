#include "MenuButtonStyleSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h" // Damp: フレームレートに依らない追従の係数

#include "Engine/UI/ECS/Components/UITextComponent.h"

#include <algorithm>

using namespace CCL::ECS;

namespace
{
    // ラベルの色。選択中は白金の帯の上に乗るので、暗い色に切り替えて読めるようにする
    const DirectX::XMFLOAT4 kLabelIdle  = {0.92f, 0.92f, 0.94f, 0.85f};
    const DirectX::XMFLOAT4 kLabelFocus = {0.06f, 0.07f, 0.10f, 1.00f};
} // namespace

void MenuButtonStyleSystem::Update(float dt)
{
    ForEach([&](const UIInteractableComponent &inter, MenuButtonVisualComponent &vis,
                UIProgressBarComponent &bar) {
        // --- 選択の帯: 選ばれていれば左から伸ばし、外れたら縮める ---
        //   帯の伸び具合を value にするので、伸びている途中は HP ゲージと同じく先端が光る
        const bool hot    = inter.interactable && (inter.isHovered || inter.isFocused);
        const float target = hot ? 1.0f : 0.0f;
        vis.focus += (target - vis.focus) * CCL::Math::Damp(kFocusSpeed, dt);
        if (std::abs(target - vis.focus) < 0.002f) vis.focus = target; // 端に張り付かせ、先端の光が残り続けないようにする
        bar.value = vis.focus;

        // --- 押した瞬間: 帯を白く飛ばして小さく揺らし、グリッチを走らせる ---
        //   白飛びと揺れはゲージの shake、グリッチは hitAge をそのまま使う
        if (inter.clicked) vis.pressAge = 0.0f;
        vis.pressAge += dt;
        const float press = std::max(0.0f, 1.0f - vis.pressAge / kPressTime);
        bar.shake         = press;
        bar.shakeElapsed  = vis.pressAge;
        bar.hitAge        = vis.pressAge;

        bar.animTime += dt; // 光沢の位相

        // --- ラベルの色: 帯の伸び具合に合わせて白から暗い色へ ---
        if (auto *label = m_world->GetComponent<UITextComponent>(vis.label))
        {
            const float t = vis.focus;
            label->color  = {kLabelIdle.x + (kLabelFocus.x - kLabelIdle.x) * t,
                             kLabelIdle.y + (kLabelFocus.y - kLabelIdle.y) * t,
                             kLabelIdle.z + (kLabelFocus.z - kLabelIdle.z) * t,
                             kLabelIdle.w + (kLabelFocus.w - kLabelIdle.w) * t};
        }
    });
}

REGISTER_RENDER_SYSTEM(MenuButtonStyleSystem, Priority::RenderStage::R10_UI);
