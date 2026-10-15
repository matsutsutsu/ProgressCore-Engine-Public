#include "Game/UI/Common/UISoundSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Audio/IAudioAPI.h"

#include <memory>

using namespace CCL::ECS;

void UISoundSystem::Play(const char *eventName)
{
    if (!m_world->HasResource<std::shared_ptr<IAudioAPI>>())
    {
        return;
    }
    if (auto &audio = m_world->GetResource<std::shared_ptr<IAudioAPI>>())
    {
        audio->PlayOneShot(CCL::Utils::HashString(eventName));
    }
}

void UISoundSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. ボタンの状態を集める
    // 2. 状態の変化に合わせて、効果音を1つ鳴らす
    // 3. 次のフレームと比べるために、今の状態を覚える

    // ---------------------------------------------------------
    // 1. ボタンの状態
    // ---------------------------------------------------------

    // 選ばれているボタンは、マウスが乗っているものを優先し、無ければパッド・キーで選んでいるものにする。
    EntityID hovered = InvalidEntityID;
    EntityID focused = InvalidEntityID;
    bool     clicked = false;
    int      count   = 0;
    ForEachWithID([&](EntityID id, const UIInteractableComponent &inter, const UIButtonComponent &) {
        ++count;
        if (inter.isHovered)
        {
            hovered = id;
        }
        if (inter.isFocused)
        {
            focused = id;
        }
        if (inter.clicked)
        {
            clicked = true;
        }
    });
    const EntityID selected = (hovered != InvalidEntityID) ? hovered : focused;

    // ---------------------------------------------------------
    // 2. 効果音
    // ---------------------------------------------------------

    // 1フレームに鳴らすのは1つだけにする。上の条件ほど優先する。
    if (clicked)
    {
        Play("sfx:/ui_confirm");
    }
    else if (m_buttonCount == 0 && count > 0)
    {
        // メニューが開いた（ポーズ・リザルトなど）。
        Play("sfx:/ui_open");
    }
    else if (m_buttonCount > 0 && count == 0 && !m_clickedLast)
    {
        // 押さずに閉じた（Esc でポーズを閉じたなど）。
        Play("sfx:/ui_close");
    }
    else if (selected != InvalidEntityID && m_selected != InvalidEntityID && selected != m_selected)
    {
        // 別のボタンへ移った。開いた瞬間の最初の選択（前のフレームで何も選ばれていない）では鳴らさない。
        Play("sfx:/ui_move");
    }

    // ---------------------------------------------------------
    // 3. 今の状態を覚える
    // ---------------------------------------------------------

    // 押した後にメニューが消えたときは、「決定」だけを鳴らして「閉じる」は重ねない。そのために押したことを覚えておく。
    // メニューが消えるのは押してから数フレーム後のこともあるので、忘れるのは次にメニューが開いたときにする。
    if (clicked)
    {
        m_clickedLast = true;
    }
    if (m_buttonCount == 0 && count > 0 && !clicked)
    {
        m_clickedLast = false;
    }

    m_selected    = selected;
    m_buttonCount = count;
}

REGISTER_RENDER_SYSTEM(UISoundSystem, Priority::RenderStage::R10_UI);
