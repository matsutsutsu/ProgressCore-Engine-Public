#include "AIPatrolSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/AI/Navigation/NavigationData.h"
#include <DetourNavMeshQuery.h>
#include <random>

// システムの実行順序の定義ヘッダー
#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// Detourのランダム関数用ヘルパー
static float dtRandom() {
    static std::mt19937 eng(std::random_device{}());
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(eng);
}

void AIPatrolSystem::Update(float dt) {
    if (!_world || !_world->HasResource<NavigationData>()) return;

    auto& navData = _world->GetResource<NavigationData>();
    dtNavMeshQuery* navQuery = navData.navQuery;
    if (!navQuery) return;

    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);
    const float extents[3] = { 2.0f, 4.0f, 2.0f };

    ForEach([&](const TransformComponent& transform, AIStateComponent& aiState, NavAgentComponent& navAgent) {

        // 徘徊ステートでなければ何もしない
        if (aiState.currentState != AIState::Patrol) return;

        aiState.timeInState += dt;

        // 1. 現在の目的地に到着しているか確認（waypointsを消化しきったか）
        bool hasReachedTarget = navAgent.waypoints.empty() ||
            (navAgent.currentWaypointIndex >= navAgent.waypoints.size());

        if (hasReachedTarget) {
            // 到着直後は少し待機（キョロキョロしている感を出す）
            if (aiState.timeInState < aiState.patrolWaitTime) {
                return; // まだ待機時間中
            }

            // ========================================================
            // ★新しいパトロール地点の検索
            // ========================================================
            float centerPos[3] = { transform.position.x, transform.position.y, transform.position.z };
            dtPolyRef startRef = 0;
            float nearestStart[3];

            // 今自分が立っているポリゴンを取得
            navQuery->findNearestPoly(centerPos, extents, &filter, &startRef, nearestStart);

            if (startRef) {
                dtPolyRef randomRef = 0;
                float randomPt[3];

                // Detourの機能：周囲半径(patrolRadius)内で、有効なランダム座標を取得
                dtStatus status = navQuery->findRandomPointAroundCircle(
                    startRef, nearestStart, aiState.patrolRadius,
                    &filter, dtRandom, &randomRef, randomPt);

                if (dtStatusSucceed(status)) {
                    // 見つけたランダム座標を、ナビゲーションエージェントの「次の目的地」にセット
                    navAgent.targetPosition = { randomPt[0], randomPt[1], randomPt[2] };
                    navAgent.isPathRequested = true; // NavPathfindingSystem に経路計算を依頼

                    // ステートタイマーをリセット
                    aiState.timeInState = 0.0f;
                }
            }
        }
        });
}

REGISTER_LOGIC_SYSTEM(AIPatrolSystem, Priority::LogicStage::L02_Update);