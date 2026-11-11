#include "ComponentGuiRegistry.h"
#include "Engine/ECS/Core/CCL_World.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

ComponentGuiRegistry &ComponentGuiRegistry::Instance()
{
    static ComponentGuiRegistry s;
    return s;
}



void ComponentGuiRegistry::Draw(
    TypeID id, void *data, EntityID entity, CCL::ECS::World *world)
{
    ResolveBindings();

    auto idIt = m_typeIdToIndex.find(id);
    if (idIt == m_typeIdToIndex.end())
    {
#ifdef ENABLE_EDITOR
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "No GUI registered for this ID.");
#endif
        return;
    }
    auto it = m_registry.find(idIt->second);
    if (it != m_registry.end())
    {
        it->second.guiFunc(data, entity, world);
    }
}