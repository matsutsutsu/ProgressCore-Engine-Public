#include "NavPathfindingSystem.h"
#include "Engine/AI/Navigation/Core/NavigationData.h"
#include <DetourNavMeshQuery.h>
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void NavPathfindingSystem::Update(float dt)
{
    // 読み書きするデータ:
    //   READ  : TransformComponent（現在地）
    //   READ  : NavigationData（ナビメッシュ。World リソース）
    //   WRITE : NavAgentComponent（waypoints の更新、isPathRequested のリセット）

    if (!m_world || !m_world->HasResource<NavigationData>()) return;

    auto           &navData  = m_world->GetResource<NavigationData>();
    dtNavMeshQuery *navQuery = navData.navQuery;
    if (!navQuery) return;

    // 探索フィルター。エリア種別を分けていないので、全ポリゴンを歩行可能として扱う
    dtQueryFilter filter;
    filter.setIncludeFlags(0xFFFF);
    filter.setExcludeFlags(0);

    // 座標から最寄りのポリゴンを探す範囲（X, Y, Z の半径）。
    // 少し宙に浮いていても見つけられるよう、Y は大きめ（4.0）に取る
    const float extents[3] = {2.0f, 4.0f, 2.0f};

    ForEach([&](const TransformComponent &transform, NavAgentComponent &agent) {
        // 要求が来た Entity だけ計算する（毎ステップ全員を計算すると重いため）
        if (!agent.isPathRequested) return;

        agent.isPathRequested = false;
        agent.waypoints.clear();
        agent.currentWaypointIndex = 0;

        float startPos[3] = {transform.position.x, transform.position.y, transform.position.z};
        float endPos[3]   = {agent.targetPosition.x, agent.targetPosition.y, agent.targetPosition.z};

        dtPolyRef startRef = 0;
        dtPolyRef endRef   = 0;
        float     nearestStart[3], nearestEnd[3];

        // 1. 現在地と目的地の最寄りのポリゴンを見つける
        navQuery->findNearestPoly(startPos, extents, &filter, &startRef, nearestStart);
        navQuery->findNearestPoly(endPos, extents, &filter, &endRef, nearestEnd);

        // ポリゴンが見つからなければ、どちらがナビメッシュの外にいるかをログに出して終える
        if (!startRef)
        {
            CCL_LOG_ERROR(LogCategory::Core, "Pathfinding Failed: AI is outside the NavMesh! AI Pos: (%.2f, %.2f, %.2f)", startPos[0], startPos[1], startPos[2]);
            return;
        }
        if (!endRef)
        {
            CCL_LOG_ERROR(LogCategory::Core, "Pathfinding Failed: Target is outside the NavMesh! Target Pos: (%.2f, %.2f, %.2f)", endPos[0], endPos[1], endPos[2]);
            return;
        }

        // 2. ポリゴン単位で経路を探す（A*）
        const int MAX_POLYS = 256;
        dtPolyRef path[MAX_POLYS];
        int       pathCount = 0;
        navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, path, &pathCount, MAX_POLYS);

        if (pathCount > 0)
        {
            // 3. ポリゴン列を、曲がり角だけを結んだ直線経路（String Pulling）に直す
            const int     MAX_WAYPOINTS = 256;
            float         straightPath[MAX_WAYPOINTS * 3];
            unsigned char straightPathFlags[MAX_WAYPOINTS];
            dtPolyRef     straightPathPolys[MAX_WAYPOINTS];
            int           straightPathCount = 0;

            navQuery->findStraightPath(nearestStart, nearestEnd, path, pathCount,
                                       straightPath, straightPathFlags,
                                       straightPathPolys, &straightPathCount, MAX_WAYPOINTS);

            for (int i = 0; i < straightPathCount; ++i)
            {
                agent.waypoints.push_back({straightPath[i * 3],
                                           straightPath[i * 3 + 1],
                                           straightPath[i * 3 + 2]});
            }

            CCL_LOG_INFO(LogCategory::Core, "Pathfinding Success! Generated %d waypoints.", straightPathCount);
        }
        else
        {
            // ポリゴンは見つかったが、道が繋がっていない
            CCL_LOG_ERROR(LogCategory::Core, "Pathfinding Failed: No valid path between start and end.");
        }
    });
}

// TODO(2026-07-11): 移動方針（Nav）を実装するときに REGISTER_LOGIC_SYSTEM で登録し直す。
//   以前の登録先ステージ（L02_AI_Think）は廃止済み。AI が目的地を決めた後、
//   NavSteeringSystem より前のステージに置くこと