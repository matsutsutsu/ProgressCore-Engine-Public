#include "Game/UI/Common/MenuStyleSystem.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Serialization/Factory/Prefab.h"

#include <imgui.h>

namespace
{
    constexpr const char *kPrefabPath = "Assets/Prefabs/UI/MenuStyle.json";
}



void MenuStyleSystem::Update(float dt)
{
    // Initialize で作らないのは、その後のレベルの読み込みで消されるため
    // 読めなくても試すのは1回だけにする（各画面は既定値で組まれる。理由は Prefab がログに出す）
    if (!m_spawned)
    {
        m_spawned = true;
        Prefab::SpawnPrefab(*m_world, kPrefabPath);
    }
}



void MenuStyleSystem::OnGui()
{
    ImGui::TextDisabled("Hierarchy の MenuStyle をインスペクタで調整し、プレハブ保存で %s へ残す", kPrefabPath);
    ImGui::TextDisabled("開いている画面は、開き直すと反映される（ボタンの文字色と帯の速さはすぐ反映）");
}



// メニューを組む System（L02_Update 以降）より前に置く
REGISTER_LOGIC_SYSTEM(MenuStyleSystem, Priority::LogicStage::L02_PreUpdate);
