/// @file  AIDebugSystem.cpp
/// @brief 司令官StateTreeの動作診断。2秒ごとに「どこで止まっているか」をログ出力。
#include "AIDebugSystem.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Game/Logics/AI/Battle/BattleContext.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;
using namespace CCL::AI;

void AIDebugSystem::Update(float dt)
{
    // 2秒に1回だけ出す（毎フレームだとログが洪水になる）
    m_timer += dt;
    if (m_timer < kInterval) return;
    m_timer = 0.0f;

    CCL_LOG_INFO(LogCategory::Game, "============================================================");
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG] === 司令官StateTree 診断 ===");

    // ---- STAGE-1: リソース確認 ----
    const bool hasReg    = m_world->HasResource<StateTreeRegistry>();
    const bool hasBattle = m_world->HasResource<BattleContext>();
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG] STAGE-1 リソース: StateTreeRegistry=%s / BattleContext=%s",
        hasReg ? "OK" : "NG", hasBattle ? "OK" : "NG");

    if (hasBattle)
    {
        auto &b = m_world->GetResource<BattleContext>();
        CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   droneAliveCount=%d  → 期待戦術=%s",
            b.droneAliveCount,
            b.droneAliveCount <= 100 ? "Final" : b.droneAliveCount <= 400 ? "Aggressive"
                                             : b.droneAliveCount <= 700   ? "Pressure"
                                                                          : "Control");
    }

    // ---- STAGE-2〜4: StateTreeComponent を持つ Entity を確認 ----
    int count = 0;
    ForEachWithID([&](EntityID                  entity,
                      const StateTreeComponent &st,
                      const TimeStateComponent & /*time*/) {
        ++count;
        CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG] --- Entity #%llu ---", (unsigned long long)entity);

        // STAGE-2: handle 解決状態
        CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   STAGE-2 assetName='%s' handle.value=%u valid=%s",
            st.assetName.c_str(), st.assetHandle.value,
            st.assetHandle.IsValid() ? "OK" : "NG(未解決)");

        // STAGE-3: どの状態が選ばれているか（名前解決はせずインデックスのみ表示）
        if (st.activeDepth == 0)
        {
            CCL_LOG_WARN(LogCategory::Game, "[AI-DIAG]   STAGE-3 activeDepth=0 ← 状態が選ばれていない");
        }
        else
        {
            for (int d = 0; d < (int)st.activeDepth; ++d)
            {
                CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   STAGE-3 active[%d] = states[%u]",
                    d, (unsigned)st.activeStates[d]);
            }
        }

        // STAGE-4: BT切替が効いているか
        const auto *bt = m_world->GetComponent<BehaviorTreeComponent>(entity);
        if (!bt)
        {
            CCL_LOG_WARN(LogCategory::Game, "[AI-DIAG]   STAGE-4 BehaviorTreeComponent が無い ← SetActiveBTが書き込めない");
        }
        else
        {
            CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   STAGE-4 assetPath='%s'", bt->assetPath.c_str());
        }
    });

    if (count == 0)
    {
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG] StateTreeComponent + TimeStateComponent を持つ Entity が0体");
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG]   → ボスに TimeStateComponent が付いていない可能性大");
    }
    CCL_LOG_INFO(LogCategory::Game, "============================================================");
}

// L02_Update の後ろで診断（StateTreeSystem が動いた後の状態を見る）
//REGISTER_LOGIC_SYSTEM(AIDebugSystem, Priority::LogicStage::L02_Update)