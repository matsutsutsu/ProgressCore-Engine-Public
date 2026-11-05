#include "AnimSetupSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void AnimSetupSystem::Update(float /*dt*/)
{
    // 処理フロー:
    // 1. 足りないコンポーネントを持つ Entity を集める
    // 2. 走査が終わってから付ける

    // コンポーネントの追加（アーキタイプの移動）を走査中に行うとイテレータが壊れるので、2 段に分ける

    std::vector<EntityID> needsParams;
    std::vector<EntityID> needsStateMachine;
    std::vector<EntityID> needsRootMotion;

    // ---------------------------------------------------------
    // 1. 対象を集める
    // ---------------------------------------------------------

    // SetExclude は使わず、足りないものを個別に調べる
    //   SetExclude<A, B, C>() は「どれか 1 つでも持っていたら除外」なので、
    //   AnimStateMachine だけをプレハブに書いた Entity が残り 2 つを受け取れなくなる
    //   対象はキャラだけで数も少ないので、毎ステップ HasComponent で調べても十分軽い
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

    // ---------------------------------------------------------
    // 2. 付ける
    // ---------------------------------------------------------
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
