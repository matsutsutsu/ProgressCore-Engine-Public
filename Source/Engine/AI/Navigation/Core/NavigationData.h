#pragma once
// ===========================================================================
// 概要: World に1つだけ置くナビゲーション用の共有データ（World リソース）。
//
// 責務:
//   - Detour のナビメッシュと経路探索クエリの所有と解放
//
// 備考: 作るのはエディタの NavMeshBuilder。
// ===========================================================================
#include <DetourNavMesh.h>
#include <DetourAlloc.h>
#include <DetourNavMeshQuery.h>

struct NavigationData
{
    dtNavMesh      *navMesh  = nullptr; // 所有する
    dtNavMeshQuery *navQuery = nullptr; // 経路探索用のクエリ（所有する）

    bool showDebugDraw = true; // NavMeshDebugDrawSystem でナビメッシュを描くか

    // Detour の確保関数で作ったものなので、対応する解放関数で解放する
    ~NavigationData()
    {
        if (navQuery)
        {
            dtFreeNavMeshQuery(navQuery);
            navQuery = nullptr;
        }
        if (navMesh)
        {
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
        }
    }
};