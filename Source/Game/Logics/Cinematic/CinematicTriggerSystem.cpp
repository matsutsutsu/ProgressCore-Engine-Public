#include "Game/Logics/Cinematic/CinematicTriggerSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Timeline/TimelineEvents.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void CinematicTriggerSystem::Update(float /*dt*/)
{
    // --- 撃破の検知 ---
    bool anyDefeated = false;
    ForEachWithID([&](EntityID id, const BossPhaseComponent &phase) {
        // 初めて見たボスは前回の形態が分からないので、入った瞬間とは扱わない
        const auto it = m_prevPhase.find(id);
        if (it != m_prevPhase.end() && it->second != BossPhase::Dead && phase.phase == BossPhase::Dead)
            anyDefeated = true;
        m_prevPhase[id] = phase.phase;
    });

    // --- 再生の要求 ---
    // ForEach を抜けてから出す。購読側（TimelineSystem）が自分の ForEach を回すので、走査中に発行しない
    if (anyDefeated)
    {
        TimelinePlayRequestEvent ev;
        ev.trigger = kBossDefeatedTrigger;
        m_world->GetEventBus().Publish(ev);
    }
}

// 形態を決める BossPhaseSystem（L02_PreUpdate）の後に見る。名前で順序を縛らずに済むよう、別のステージに置く
REGISTER_LOGIC_SYSTEM(CinematicTriggerSystem, Priority::LogicStage::L02_PostUpdate);
