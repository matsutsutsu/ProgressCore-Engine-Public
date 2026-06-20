/// @file  AIDebugSystem.cpp
/// @brief AI パイプライン診断システムの実装。
#include "AIDebugSystem.h"

#include "Game/Logics/AI/StateTree/Core/StateTreeRegistry.h"
#include "Game/Logics/AI/StateTree/Core/NodeRegistry.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"          // パスは環境に合わせる
#include "Game/Logics/AI/FlowField/NavGridFlowField.h"           // パスは環境に合わせる

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;
using namespace CCL::AI;

AIDebugSystem::AIDebugSystem() : IfSystem("AIDebugSystem") {}

void AIDebugSystem::Update(float dt)
{
    _timer += dt;
    _frameCount += 1;

    // 初回フレームと 2 秒おきだけ出力
    if (!_firstFrame && _timer < _interval) return;
    _firstFrame = false;
    _timer = 0.0f;

    CCL_LOG_INFO(LogCategory::Game,
        "============================================================");
    CCL_LOG_INFO(LogCategory::Game,
        "[AI-DIAG] === AI パイプライン診断 (frame=%d) ===", _frameCount);
    CCL_LOG_INFO(LogCategory::Game,
        "============================================================");

    // ================================================================
    // STAGE-1: Resource の存在確認
    // ================================================================
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG] --- STAGE-1: Resource 確認 ---");

    const bool hasDungeon = _world->HasResource<RuneForge::Dungeon::DungeonGrid>();
    const bool hasFlowField = _world->HasResource<RuneForge::Dungeon::NavGridFlowField>();
    const bool hasSTReg = _world->HasResource<StateTreeRegistry>();

    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   DungeonGrid      : %s",
        hasDungeon ? "OK" : "NG ← AddResource<DungeonGrid>() が呼ばれていない");
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   NavGridFlowField : %s",
        hasFlowField ? "OK" : "NG ← NavFlowFieldSystem が L01 で動いていないかも");
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG]   StateTreeRegistry: %s",
        hasSTReg ? "OK" : "NG ← Scene 初期化で AddResource<StateTreeRegistry>() が必要");

    if (!hasSTReg) {
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG] ★ StateTreeRegistry がない → STAGE-4 以降は確認不可");
    }

    // ================================================================
    // STAGE-2: NavFlowField の中身確認
    // ================================================================
    CCL_LOG_INFO(LogCategory::Game, "[AI-DIAG] --- STAGE-2: NavFlowField 確認 ---");

    if (hasFlowField) {
        auto& ff = _world->GetResource<RuneForge::Dungeon::NavGridFlowField>();
        CCL_LOG_INFO(LogCategory::Game,
            "[AI-DIAG]   valid=%s  size=(%d x %d)  target=(%d,%d)",
            ff.valid ? "true" : "false ← BFS が完走していない（プレイヤー未スポーン？）",
            ff.width, ff.depth, ff.targetI, ff.targetJ);

        if (ff.valid && ff.width > 0) {
            // プレイヤーセル周辺の dist を抜き打ちで表示
            int ti = ff.targetI, tj = ff.targetJ;
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   dist[target]  = %d (0 が正常)", ff.Dist(ti, tj));
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   dist[t+1,t]   = %d", ff.Dist(ti + 1, tj));
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   dist[t,t+1]   = %d", ff.Dist(ti, tj + 1));
        }
    }
    else {
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG]   NavGridFlowField が未登録 → NavFlowFieldSystem を確認");
    }

    // ================================================================
    // STAGE-3〜7: 全敵 Entity を ForEach でチェック
    // ================================================================
    CCL_LOG_INFO(LogCategory::Game,
        "[AI-DIAG] --- STAGE-3〜7: 敵 Entity の状態確認 ---");

    int entityCount = 0;

    ForEachWithID([&](EntityID entity,
        const StateTreeComponent& st,
        const PerceptionComponent& perc,
        const MoveIntentComponent& mi,
        const TransformComponent& trans)
        {
            entityCount++;

            // 最初の5体だけ詳細ログ（多すぎると洪水になるため）
            if (entityCount > 5) return;

            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG] --- Entity #%llu (pos=%.1f,%.1f,%.1f) ---",
                (unsigned long long)entity,
                trans.position.x, trans.position.y, trans.position.z);

            // STAGE-3: StateTreeComponent のアセットハンドル
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   [STAGE-3] assetHandle.value = %u  valid=%s",
                st.assetHandle.value,
                st.assetHandle.IsValid()
                ? "OK"
                : "NG ← Spawn時に st.assetHandle をセットしていない");

            // STAGE-4: アセットの中身
            if (st.assetHandle.IsValid() && hasSTReg) {
                auto& reg = _world->GetResource<StateTreeRegistry>();
                const StateTreeAsset* asset = reg.Get(st.assetHandle);
                if (asset) {
                    CCL_LOG_INFO(LogCategory::Game,
                        "[AI-DIAG]   [STAGE-4] Asset='%s'  states=%zu  tasks=%zu",
                        asset->name.c_str(),
                        asset->states.size(),
                        asset->tasks.size());
                }
                else {
                    CCL_LOG_WARN(LogCategory::Game,
                        "[AI-DIAG]   [STAGE-4] NG ← handle=%u に対応するアセットが Registry にない",
                        st.assetHandle.value);
                }
            }

            // STAGE-5: Perception（プレイヤー知覚）
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   [STAGE-5] target=%llu  distance=%.2f  lastKnown=(%.1f,%.1f,%.1f)",
                (unsigned long long)perc.target,
                perc.distance,
                perc.lastKnownPos.x, perc.lastKnownPos.y, perc.lastKnownPos.z);

            if (perc.target == CCL::ECS::InvalidEntityID) {
                CCL_LOG_WARN(LogCategory::Game,
                    "[AI-DIAG]   [STAGE-5] NG ← プレイヤーが見えていない。"
                    "EnemyPerceptionSystem が動いているか？ / PlayerComponent が存在するか？");
            }

            // STAGE-6: StateTree のアクティブ状態
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   [STAGE-6] activeDepth=%u", (unsigned)st.activeDepth);

            if (st.activeDepth == 0) {
                CCL_LOG_WARN(LogCategory::Game,
                    "[AI-DIAG]   [STAGE-6] NG ← 状態が1つも選ばれていない。"
                    "StateTreeSystem が動いているか？ / StateTreeRegistry が空ではないか？");
            }
            else if (hasSTReg) {
                auto& reg = _world->GetResource<StateTreeRegistry>();
                const StateTreeAsset* asset = reg.Get(st.assetHandle);
                if (asset) {
                    for (int d = 0; d < (int)st.activeDepth; ++d) {
                        CCL_LOG_INFO(LogCategory::Game,
                            "[AI-DIAG]   [STAGE-6]   [%d] states[%u] = '%s'",
                            d, (unsigned)st.activeStates[d],
                            asset->GetStateName(st.activeStates[d]).c_str());
                    }
                }
            }

            // STAGE-7: MoveIntent（移動意図）
            CCL_LOG_INFO(LogCategory::Game,
                "[AI-DIAG]   [STAGE-7] hasTarget=%s  speed=%.2f  target=(%.1f,%.1f,%.1f)",
                mi.hasTarget ? "true" : "false ← MoveToTargetTask が Tick されていない",
                mi.desiredSpeed,
                mi.desiredTarget.x, mi.desiredTarget.y, mi.desiredTarget.z);

            if (!mi.hasTarget) {
                CCL_LOG_WARN(LogCategory::Game,
                    "[AI-DIAG]   [STAGE-7] NG ← Idle 状態のまま。"
                    "STAGE-5 でプレイヤーが見えていれば Chase になるはず。");
            }
        });

    if (entityCount == 0) {
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG] ★ StateTreeComponent を持つ Entity が1体もいない！");
        CCL_LOG_WARN(LogCategory::Game,
            "[AI-DIAG]   → Archetype に StateTreeComponent / PerceptionComponent /"
            " MoveIntentComponent が全部含まれているか確認");
    }
    else {
        CCL_LOG_INFO(LogCategory::Game,
            "[AI-DIAG] 敵 Entity 合計: %d 体 (詳細は最初の5体のみ表示)", entityCount);
    }

    CCL_LOG_INFO(LogCategory::Game,
        "============================================================");
}

REGISTER_LOGIC_SYSTEM(AIDebugSystem, Priority::LogicStage::L02_Update)