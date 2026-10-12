#include "Game/Logics/Cinematic/TimelineRoleBindSystem.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Engine/ECS/Core/CCL_World.h"

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void TimelineRoleBindSystem::Update(float /*dt*/)
{
    // BattleContext は L01_WorldState で集計されるので、ここで読むのは前ステップの値になる。
    // 開始待ちの間に1ステップ遅れて結び付くだけなので実害は無い。
    if (!m_world->HasResource<BattleContext>()) return;
    const auto &battle = m_world->GetResource<BattleContext>();

    ForEach([&](TimelinePlayerComponent &player) {
        // 居ない役割は書かない（消すと、ボス撃破後のムービーでボスの役割まで外れてしまう）
        if (battle.HasPlayer()) player.bindings["Player"] = battle.playerEntity;
        if (battle.HasBoss()) player.bindings["Boss"] = battle.bossEntity;
    });
}

REGISTER_LOGIC_SYSTEM(TimelineRoleBindSystem, Priority::LogicStage::L01_Input);
