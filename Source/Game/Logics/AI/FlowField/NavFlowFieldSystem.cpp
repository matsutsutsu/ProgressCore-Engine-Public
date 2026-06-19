/// @file  NavFlowFieldSystem.cpp
/// @brief NavFlowFieldSystem の実装。プレイヤーセルから 4 近傍 BFS で距離場を作る。
#include "NavFlowFieldSystem.h"

#include "NavGridFlowField.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"                 // パスは適宜合わせてください
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

NavFlowFieldSystem::NavFlowFieldSystem() : IfSystem("NavFlowFieldSystem") {}

void NavFlowFieldSystem::Update(float /*dt*/)
{
    // ------------------------------------------------------------
    // 0. 出力先 Resource を用意（初回だけ生成）
    // ------------------------------------------------------------
    if (!_world->HasResource<NavGridFlowField>())
        _world->AddResource<NavGridFlowField>();
    auto& field = _world->GetResource<NavGridFlowField>();

    // ------------------------------------------------------------
    // 1. DungeonGrid が無ければ無効化して終了（非ダンジョンScene対応）
    // ------------------------------------------------------------
    if (!_world->HasResource<DungeonGrid>()) {
        field.valid = false;
        return;
    }
    auto& grid = _world->GetResource<DungeonGrid>();

    // ------------------------------------------------------------
    // 2. プレイヤー位置 → セル
    // ------------------------------------------------------------
    bool found = false;
    int  pi = 0, pj = 0;
    auto playerView = _world->View<PlayerComponent, TransformComponent>();
    for (auto e : playerView) {
        auto* t = _world->GetComponent<TransformComponent>(e);
        grid.WorldToCell(t->position.x, t->position.z, pi, pj);
        found = true;
        break;
    }
    if (!found) { field.valid = false; return; }

    // ------------------------------------------------------------
    // 3. 距離場を初期化（バッファは再利用：同サイズなら再確保されない）
    // ------------------------------------------------------------
    const int w = grid.Width();
    const int d = grid.Depth();
    field.width = w;
    field.depth = d;
    field.targetI = pi;
    field.targetJ = pj;
    field.dist.assign(static_cast<size_t>(w) * d, -1);

    // プレイヤーが壁の中（範囲外含む）なら距離場なし
    if (grid.IsWall(pi, pj)) { field.valid = true; return; }

    // ------------------------------------------------------------
    // 4. 4 近傍 BFS（インデックス = i*depth + j で平坦化）
    // ------------------------------------------------------------
    auto idx = [d](int i, int j) -> int { return i * d + j; };

    _bfsQueue.clear();
    field.dist[idx(pi, pj)] = 0;
    _bfsQueue.push_back(idx(pi, pj));

    const int di[4] = { 1, -1, 0, 0 };
    const int dj[4] = { 0, 0, 1, -1 };

    size_t head = 0;
    while (head < _bfsQueue.size()) {
        const int cur = _bfsQueue[head++];
        const int ci = cur / d;
        const int cj = cur % d;
        const int cd = field.dist[cur];

        for (int k = 0; k < 4; ++k) {
            const int ni = ci + di[k];
            const int nj = cj + dj[k];
            if (ni < 0 || nj < 0 || ni >= w || nj >= d) continue;
            if (grid.IsWall(ni, nj)) continue;

            const int nIdx = idx(ni, nj);
            if (field.dist[nIdx] != -1) continue;   // 訪問済み

            field.dist[nIdx] = cd + 1;
            _bfsQueue.push_back(nIdx);
        }
    }

    field.valid = true;
}

REGISTER_LOGIC_SYSTEM(NavFlowFieldSystem, Priority::LogicStage::L01_Input)