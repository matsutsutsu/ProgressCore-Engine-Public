/// @file  BattleContextSyncSystem.cpp
/// @brief プレイヤー・ボスを探して BattleContext に反映する実装。
#include "BattleContextSyncSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"

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
    

    // --- ボスを探して反映 ---
    // 今はボス１体だけなので最初の１体だけ取得
    // 今後複数体敵が出るので改良しておく
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


// AI層(L02〜)が読むより前に「世界の事実」を確定させる。
//   L01_WorldState はそのための専用ステージ。入力受付(L01_Input)と混ぜないのは、
//   1フレームの段取りを「入力 → 事実 → 感知 → 判断 → 行動」と一列に読めるようにするため。
REGISTER_LOGIC_SYSTEM(BattleContextSyncSystem, Priority::LogicStage::L01_WorldState);
