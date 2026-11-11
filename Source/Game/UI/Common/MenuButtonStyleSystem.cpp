#include "Game/UI/Common/MenuButtonStyleSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Interpolation.h"

#include "Engine/UI/ECS/Components/UITextComponent.h"
#include "Game/UI/Common/MenuUiFactory.h"

#include <algorithm>

using namespace CCL::ECS;

void MenuButtonStyleSystem::Update(float dt)
{
    // --- 見た目の値 ---
    // 毎フレーム探さないよう、見つけた MenuStyle の ID を持っておく
    static const MenuStyleComponent kDefaultStyle{};
    const auto                     *found = m_world->GetComponent<MenuStyleComponent>(m_styleID);
    if (!found)
    {
        m_styleID = CCL::GameUI::FindMenuStyle(*m_world);
        found     = m_world->GetComponent<MenuStyleComponent>(m_styleID);
    }
    const MenuStyleComponent &style = found ? *found : kDefaultStyle;

    // ラベルの色。選択中は白金の帯の上に乗るので、暗い色に切り替えて読めるようにする
    const DirectX::XMFLOAT4 &idle    = style.labelIdle;
    const DirectX::XMFLOAT4 &focused = style.labelFocus;

    ForEach([&](const UIInteractableComponent &inter, MenuButtonVisualComponent &vis,
                UIProgressBarComponent &bar) {
        // --- 選択の帯 ---
        // 選ばれていれば左から伸ばし、外れたら縮める
        // 伸び具合を value にするので、伸びている途中は HP ゲージと同じく先端が光る
        const bool  hot    = inter.interactable && (inter.isHovered || inter.isFocused);
        const float target = hot ? 1.0f : 0.0f;
        vis.focus += (target - vis.focus) * CCL::Math::Damp(style.focusSpeed, dt);

        // 端に張り付かせ、先端の光が残り続けないようにする
        if (std::abs(target - vis.focus) < 0.002f) vis.focus = target;
        bar.value = vis.focus;

        // --- 押した瞬間 ---
        // 帯を白く飛ばして小さく揺らし、グリッチを走らせる
        // 白飛びと揺れはゲージの shake、グリッチは hitAge をそのまま使う
        if (inter.clicked) vis.pressAge = 0.0f;
        vis.pressAge += dt;
        const float press = std::max(0.0f, 1.0f - vis.pressAge / std::max(style.pressTime, 0.01f));
        bar.shake         = press;
        bar.shakeElapsed  = vis.pressAge;
        bar.hitAge        = vis.pressAge;

        bar.animTime += dt; // 光沢の位相

        // --- ラベルの色 ---
        // 帯の伸び具合に合わせて白から暗い色へ
        if (auto *label = m_world->GetComponent<UITextComponent>(vis.label))
        {
            const float t = vis.focus;
            label->color  = {idle.x + (focused.x - idle.x) * t,
                             idle.y + (focused.y - idle.y) * t,
                             idle.z + (focused.z - idle.z) * t,
                             idle.w + (focused.w - idle.w) * t};
        }
    });
}



REGISTER_RENDER_SYSTEM(MenuButtonStyleSystem, Priority::RenderStage::R10_UI);
