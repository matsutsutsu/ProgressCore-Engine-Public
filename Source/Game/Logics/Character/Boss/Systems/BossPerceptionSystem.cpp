/// @file  BossPerceptionSystem.cpp
/// @brief ボスの状況認識の実装。BattleContext（世界の事実）を読んで
///        blackboard（各ボスの主観的認識）を更新する。
///
/// BattleContext.playerPos を使うことで、「プレイヤーをViewで探す」処理を
/// 各システムで重複させない（1対1なので探索はBattleContext更新側で1回のみ）。
#include "BossPerceptionSystem.h"

#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <SimpleMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void BossPerceptionSystem::Update(float /*rawDt*/)
{
    if (!_world->HasResource<BattleContext>()) return;
    const auto &battle = _world->GetResource<BattleContext>();
    if (!battle.HasPlayer()) return; // プレイヤーがまだいなければ何もしない

    ForEach([&](BehaviorTreeComponent    &btComp,
                const TransformComponent &trans,
                const HealthComponent    &health,
                const TimeStateComponent & /*time*/) {
        // --- HP割合の同期 ---
        if (health.maxHealth > 0.0f)
        {
            btComp.blackboard.healthPercentage =
                (health.currentHealth / health.maxHealth) * 100.0f;
        }

        // --- プレイヤーとの距離の同期（世界の事実 BattleContext を使う）---
        Vector3 myPos(trans.position);
        Vector3 playerPos(battle.playerPos);
        btComp.blackboard.distanceToTarget = Vector3::Distance(myPos, playerPos);
        btComp.blackboard.targetPosition   = playerPos;
    });
}

// BehaviorTreeSystem (L02) より前に認識を最新化しておく
REGISTER_LOGIC_SYSTEM(BossPerceptionSystem, Priority::LogicStage::L01_Input);