#include "Game/UI/Common/UISoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include <memory>

using namespace CCL::ECS;

void UISoundSystem::Play(const char *eventName)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>()) return;
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
        audio->PlayOneShot(CCL::Utils::HashString(eventName));
}

void UISoundSystem::Update(float /*dt*/)
{
    // 選ばれているボタン（マウスが乗っているものを優先し、無ければパッド・キーで選んでいるもの）と、押されたか
    EntityID hovered = InvalidEntityID;
    EntityID focused = InvalidEntityID;
    bool     clicked = false;
    int      count   = 0;
    ForEachWithID([&](EntityID id, const UIInteractableComponent &inter, const UIButtonComponent &) {
        ++count;
        if (inter.isHovered) hovered = id;
        if (inter.isFocused) focused = id;
        if (inter.clicked) clicked = true;
    });
    const EntityID selected = (hovered != InvalidEntityID) ? hovered : focused;

    if (clicked)
    {
        Play("sfx:/ui_confirm");
    }
    else if (m_buttonCount == 0 && count > 0)
    {
        Play("sfx:/ui_open"); // メニューが開いた（ポーズ・リザルトなど）
    }
    else if (m_buttonCount > 0 && count == 0 && !m_clickedLast)
    {
        Play("sfx:/ui_close"); // 押さずに閉じた（Esc でポーズを閉じたなど）
    }
    else if (selected != InvalidEntityID && m_selected != InvalidEntityID && selected != m_selected)
    {
        Play("sfx:/ui_move"); // 別のボタンへ移った（開いた瞬間の最初の選択では鳴らさない）
    }

    // 押した後にメニューが消えたときは「決定」だけを鳴らしたいので、押したことを覚えておく。
    //   消すのは数フレーム後のこともあるので、忘れるのは次にメニューが開いたときにする
    if (clicked) m_clickedLast = true;
    if (m_buttonCount == 0 && count > 0 && !clicked) m_clickedLast = false;

    m_selected    = selected;
    m_buttonCount = count;
}

REGISTER_RENDER_SYSTEM(UISoundSystem, Priority::RenderStage::R10_UI);
