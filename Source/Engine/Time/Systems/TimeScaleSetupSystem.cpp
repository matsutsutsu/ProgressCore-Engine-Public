#include "Engine/Time/Systems/TimeScaleSetupSystem.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Components/EntityLabel.h"

using namespace CCL::ECS;

void TimeScaleSetupSystem::Update(float dt)
{
    // ForEach 中は World::HasComponent で判定するだけにし、付与はループの後で行う
    std::vector<EntityID> needsTimeState;

    // 1. Transform を持つが TimeState を持たない Entity を集める
    ForEachWithID([&](EntityID id, const TransformComponent &transform) {
        if (!m_world->HasComponent<TimeStateComponent>(id))
        {
            needsTimeState.push_back(id);
        }
    });

    // 2. 足りない Entity に一括で付ける（プレハブの設定漏れの救済）
    for (auto id : needsTimeState)
    {
        m_world->AddComponent<TimeStateComponent>(id);

        // 付与のたびに Archetype 遷移が起きて Chunk が細切れになるので、警告でプレハブの修正を促す
        CCL_LOG_WARN(LogCategory::Game,
                     "TimeState was automatically added to %s. "
                     "To avoid chunk fragmentation, please add TimeState in the Editor Prefab.",
                     CCL::EntityLabel(*m_world, id).c_str());
    }
}



// TimeScaleSystem が localDt を書く前に TimeState をそろえておくため、1段前のステージに置く
REGISTER_LOGIC_SYSTEM(TimeScaleSetupSystem, Priority::LogicStage::L01_TimeSetup)
