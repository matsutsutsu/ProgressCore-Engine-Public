#include "MaterialSetupSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void MaterialSetupSystem::Update(float dt)
{
    std::vector<EntityID> needMaterial;

    ForEachWithID([&](EntityID id, const ModelComponent &model) {
        needMaterial.push_back(id);
    });

    for (auto id : needMaterial)
    {
        m_world->AddComponent<MaterialComponent>(id);
    }
}



REGISTER_LOGIC_SYSTEM(MaterialSetupSystem, Priority::LogicStage::L02_PostUpdate);