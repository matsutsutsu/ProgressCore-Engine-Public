#include "MaterialSetupSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void MaterialSetupSystem::Update(float dt)
{
    std::vector<EntityID> needMaterial;

    // 1. 足りないコンポーネントを【個別に】調査する
    ForEachWithID([&](EntityID id, const ModelComponent &model) {
        needMaterial.push_back(id);
    });

    // 2. 足りないものだけを【個別に】付与する
    for (auto id : needMaterial)
    {
        m_world->AddComponent<MaterialComponent>(id);
    }
}

// ロジックフェーズ、かつトランスフォーム更新の直後あたりに登録
REGISTER_LOGIC_SYSTEM(MaterialSetupSystem, Priority::LogicStage::L02_PostUpdate);