#include "AnimSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void AnimSetupSystem::Update(float /*dt*/)
{
    // コンポーネントの追加（アーキタイプの移動）を走査中に行うとイテレータが壊れるので、
    // 対象の ID を先に集めてから付ける
    std::vector<EntityID> needsParams;
    std::vector<EntityID> needsStateMachine;
    std::vector<EntityID> needsRootMotion;

    // 足りないコンポーネントを個別に調べる
    ForEachWithID([&](EntityID id, const AnimatorComponent &animator) {
        if (!m_world->HasComponent<AnimParametersComponent>(id))
        {
            needsParams.push_back(id);
        }
        if (!m_world->HasComponent<AnimStateMachineComponent>(id))
        {
            needsStateMachine.push_back(id);
        }
        if (!m_world->HasComponent<RootMotionComponent>(id))
        {
            needsRootMotion.push_back(id);
        }
    });

    // 走査が終わってから、足りないものだけを付ける
    for (auto id : needsParams)
    {
        m_world->AddComponent<AnimParametersComponent>(id);
    }
    for (auto id : needsStateMachine)
    {
        m_world->AddComponent<AnimStateMachineComponent>(id);
    }
    for (auto id : needsRootMotion)
    {
        m_world->AddComponent<RootMotionComponent>(id);
    }
}

// 同じステージの AnimControllerSystem は GetRunAfter でこの System の後に走る
REGISTER_LOGIC_SYSTEM(AnimSetupSystem, Priority::LogicStage::L02_Update);