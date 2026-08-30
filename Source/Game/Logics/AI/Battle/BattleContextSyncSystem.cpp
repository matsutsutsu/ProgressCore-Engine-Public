/// @file  BattleContextSyncSystem.cpp
/// @brief プレイヤー・ボスを探して BattleContext に反映する実装。
#include "BattleContextSyncSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h" // BossCommandComponent

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;


void BattleContextSyncSystem::Initialize()
{
    EnsureContext();
}

void BattleContextSyncSystem::EnsureContext()
{
    if (m_world->HasResource<BattleContext>()) return;
    m_world->AddResource<BattleContext>();
}

void BattleContextSyncSystem::Update(float /*dt*/)
{
    EnsureContext();
    auto& battle = m_world->GetResource<BattleContext>();

    // --- プレイヤーを探して反映 ---
    {
        // 1対1なので最初の1体でよい。毎フレーム走るため確保はしない。
        EntityID playerId = InvalidEntityID;
        m_world->ViewEach<PlayerComponent, TransformComponent>([&](EntityID e) {
            if (playerId == InvalidEntityID) playerId = e;
        });

        battle.playerEntity = playerId;
        if (playerId != InvalidEntityID) {
            if (auto* t = m_world->GetComponent<TransformComponent>(playerId)) {
                battle.playerPos = t->position;
            }
        }
    }

    // --- ボスを探して反映 ---
    {
        EntityID bossId = InvalidEntityID;
        m_world->ViewEach<BossCommandComponent, TransformComponent>([&](EntityID e) {
            if (bossId == InvalidEntityID) bossId = e;
        });

        battle.bossEntity = bossId;
        if (bossId != InvalidEntityID) {
            if (auto* t = m_world->GetComponent<TransformComponent>(bossId)) {
                battle.bossPos = t->position;
            }
        }
    }
}


// あらゆるAI/移動系システムより前に、BattleContextを最新化しておく
REGISTER_LOGIC_SYSTEM(BattleContextSyncSystem, Priority::LogicStage::L01_Input);