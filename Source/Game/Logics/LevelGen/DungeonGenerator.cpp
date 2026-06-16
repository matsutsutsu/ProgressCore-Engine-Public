//============================================================================
//  DungeonGenerator.cpp
//  グリッドグロース型生成（Enter the Gungeon 風）
//    A: グラフ成長（仮想グリッド上で上下左右に部屋を生やす）
//    B: 各ノードにテンプレート割り当て（回転込み）
//    C: 部屋セル → 実タイルグリッドへ展開（焼き込み）
//    D: 隣接部屋を通路で接続
//============================================================================
#include "DungeonGenerator.h"
#include "RoomTemplateTable.h"

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>
#include <queue>

#if defined(RUNEFORGE_HAS_LOGGER)
#include "Engine/Platform/Core/Logger.h"
#endif

namespace RuneForge::Dungeon
{
    //========================================================================
    //  決定的乱数 (splitmix64)
    //========================================================================
    class Rng
    {
    public:
        explicit Rng(uint64_t seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
        uint32_t NextU32()
        {
            uint64_t z = (_state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z = z ^ (z >> 31);
            return (uint32_t)(z >> 32);
        }
        int RangeInt(int n) { if (n <= 0) return 0; return (int)(NextU32() % (uint32_t)n); }
        int RangeInt(int lo, int hi) { if (hi <= lo) return lo; return lo + RangeInt(hi - lo + 1); }
        float Next01() { return (NextU32() & 0xFFFFFF) / (float)0x1000000; }
        template<typename T> const T& Pick(const std::vector<T>& v) { return v[RangeInt((int)v.size())]; }
    private:
        uint64_t _state;
    };

    //========================================================================
    //  フェーズB: テンプレート選択（型に合うものから1つ選び、許可された回転を適用）
    //========================================================================
    static RoomTemplate SelectTemplate(Rng& rng, RoomType type)
    {
        std::vector<int> candidates = GetTemplatesForType(type);
        const auto& table = GetRoomTemplateTable();

        if (candidates.empty())
        {
            // フォールバック: 該当なしなら最初のテンプレートを使う
            RoomTemplate t = table.empty() ? RoomTemplate{} : table[0];
            return t;
        }

        int pick = candidates[rng.RangeInt((int)candidates.size())];
        const RoomTemplate& base = table[pick];

        // 許可された回転からランダムに選ぶ
        int rot = 0;
        if (!base.allowedRotations.empty())
            rot = base.allowedRotations[rng.RangeInt((int)base.allowedRotations.size())];
        return RotateTemplate(base, rot);
    }

    //========================================================================
    //  フェーズA: グリッドグロース型グラフ生成
    //========================================================================
    struct RoomNode
    {
        RoomType type = RoomType::Combat;
        int rx = 0, ry = 0;             // 部屋セル座標（仮想グリッド）
        int distFromStart = -1;         // BFS距離（ゴール選定用）
        std::vector<int> neighbors;     // 接続先ノードidx
    };

    struct RoomGraph
    {
        std::vector<RoomNode> nodes;
        int startIdx = 0;
        int goalIdx = 0;
        int minRX = 0, maxRX = 0, minRY = 0, maxRY = 0;
    };

    static const int kDX[4] = { 1, -1, 0, 0 };
    static const int kDY[4] = { 0, 0, 1, -1 };

    static RoomGraph BuildRoomGraph(Rng& rng, int floorIndex, const DungeonGenParams& p)
    {
        RoomGraph G;

        // 部屋セルの占有マップ（簡易: 線形探索でも 8〜12 部屋なら十分軽い）
        auto findAt = [&](int x, int y) -> int {
            for (int i = 0; i < (int)G.nodes.size(); ++i)
                if (G.nodes[i].rx == x && G.nodes[i].ry == y) return i;
            return -1;
            };

        // 層ごとに部屋数を増やす
        int bonus = (floorIndex - 1) * p.roomsPerFloorBonus;
        int lo = p.minRooms + bonus;
        int hi = p.maxRooms + bonus;
        int target = lo + rng.RangeInt(hi - lo + 1);
        target = std::max(2, target);

        // スタートを原点に
        G.nodes.push_back({ RoomType::Start, 0, 0, 0, {} });

        std::queue<int> frontier;
        frontier.push(0);

        while ((int)G.nodes.size() < target && !frontier.empty())
        {
            int cur = frontier.front(); frontier.pop();
            int cx = G.nodes[cur].rx, cy = G.nodes[cur].ry;

            // この部屋から伸ばす方向数（バランス型）
            int sprouts = 1 + ((rng.Next01() < p.branchChance) ? 1 : 0);
            sprouts = std::clamp(sprouts, 1, p.maxBranchPerRoom);

            // 方向シャッフル
            int dirs[4] = { 0,1,2,3 };
            for (int i = 3; i > 0; --i) { int j = rng.RangeInt(i + 1); std::swap(dirs[i], dirs[j]); }

            int made = 0;
            for (int d = 0; d < 4 && made < sprouts && (int)G.nodes.size() < target; ++d)
            {
                int nx = cx + kDX[dirs[d]], ny = cy + kDY[dirs[d]];
                if (findAt(nx, ny) >= 0) continue; // 既に部屋がある
                int idx = (int)G.nodes.size();
                G.nodes.push_back({ RoomType::Combat, nx, ny, -1, {} });
                G.nodes[cur].neighbors.push_back(idx);
                G.nodes[idx].neighbors.push_back(cur);
                frontier.push(idx);
                ++made;
            }
            // まだ伸ばせる余地があれば自身を frontier に戻す（成長を均す）
            if (made > 0 && rng.Next01() < p.regrowChance) frontier.push(cur);
        }

        // BFS距離
        for (auto& n : G.nodes) n.distFromStart = -1;
        G.nodes[0].distFromStart = 0;
        std::queue<int> bfs; bfs.push(0);
        while (!bfs.empty())
        {
            int c = bfs.front(); bfs.pop();
            for (int nb : G.nodes[c].neighbors)
                if (G.nodes[nb].distFromStart < 0)
                {
                    G.nodes[nb].distFromStart = G.nodes[c].distFromStart + 1;
                    bfs.push(nb);
                }
        }

        // ゴール = スタートから最遠ノード
        int goal = 0, maxd = -1;
        for (int i = 0; i < (int)G.nodes.size(); ++i)
            if (G.nodes[i].distFromStart > maxd) { maxd = G.nodes[i].distFromStart; goal = i; }
        G.startIdx = 0;
        G.goalIdx = goal;

        // ゴール部屋の型: 第5層はボス、それ以外はポータル
        bool isBossFloor = (floorIndex >= 5);
        G.nodes[goal].type = isBossFloor ? RoomType::Boss : RoomType::Portal;

        // 第5層はボスの「隣（スタート寄り）」にポータルを置きたいが、簡易にゴールをボスにして
        // ゴールの最遠の隣をポータルにする。ここではゴール=ボスとし、別途ポータルを最遠の
        // 2番目に割り当てる。
        if (isBossFloor)
        {
            int portal = -1, pd = -1;
            for (int i = 0; i < (int)G.nodes.size(); ++i)
                if (i != goal && G.nodes[i].distFromStart > pd) { pd = G.nodes[i].distFromStart; portal = i; }
            if (portal >= 0) G.nodes[portal].type = RoomType::Portal;
        }

        // --- 必須部屋（ショップ/鍛冶/宝物庫/エリート）を、Start/Goal/Portal以外の
        //     行き止まり（neighbors==1）や中間ノードに割り当てる ---
        if (p.placeSpecialRooms)
        {
            std::vector<int> assignable;
            for (int i = 0; i < (int)G.nodes.size(); ++i)
            {
                if (i == G.startIdx || i == G.goalIdx) continue;
                if (G.nodes[i].type != RoomType::Combat) continue; // 既に特殊型は除外
                assignable.push_back(i);
            }
            // 行き止まりを優先（袋小路に宝物庫やショップがあると探索の意味が出る）
            std::sort(assignable.begin(), assignable.end(), [&](int a, int b) {
                return G.nodes[a].neighbors.size() < G.nodes[b].neighbors.size();
                });

            auto assign = [&](RoomType t) {
                if (assignable.empty()) return;
                int idx = assignable.front();
                assignable.erase(assignable.begin());
                G.nodes[idx].type = t;
                };
            assign(RoomType::Shop);
            assign(RoomType::Forge);
            assign(RoomType::Treasury);
            if (rng.RangeInt(2) == 0) assign(RoomType::Treasury);
            assign(RoomType::Elite);
        }

        // 範囲
        G.minRX = G.maxRX = G.minRY = G.maxRY = 0;
        for (auto& n : G.nodes)
        {
            G.minRX = std::min(G.minRX, n.rx); G.maxRX = std::max(G.maxRX, n.rx);
            G.minRY = std::min(G.minRY, n.ry); G.maxRY = std::max(G.maxRY, n.ry);
        }
        return G;
    }

    //========================================================================
    //  配置済み部屋（実タイルグリッド上）
    //========================================================================
    struct PlacedRoom
    {
        RoomType type;
        int offI = 0, offJ = 0;     // 実グリッド左下オフセット
        RoomTemplate tpl;
        int nodeIdx = -1;           // 対応するグラフノード
        std::vector<SpawnMarker> worldSpawns;
    };

    struct GenContext
    {
        std::vector<PlacedRoom> placed;   // graph.nodes と同順
        int gridW = 0, gridH = 0;
        int startNode = 0, goalNode = 0;
    };

    static CellPos RoomCenter(const PlacedRoom& pr)
    {
        return { pr.offI + pr.tpl.width / 2, pr.offJ + pr.tpl.height / 2 };
    }

    //========================================================================
    //  フェーズC: 部屋セル → 実タイルグリッドへ展開
    //========================================================================
    static void PlaceRooms(Rng& rng, const RoomGraph& graph, const DungeonGenParams& p,
        GenContext& ctx)
    {
        const int cellW = p.cellTilesW + p.cellGap; // 1部屋セルが占める実タイル幅
        const int cellH = p.cellTilesH + p.cellGap;
        const int margin = 2;

        int spanX = (graph.maxRX - graph.minRX + 1);
        int spanY = (graph.maxRY - graph.minRY + 1);
        ctx.gridW = margin * 2 + spanX * cellW;
        ctx.gridH = margin * 2 + spanY * cellH;
        ctx.startNode = graph.startIdx;
        ctx.goalNode = graph.goalIdx;

        ctx.placed.resize(graph.nodes.size());
        for (int i = 0; i < (int)graph.nodes.size(); ++i)
        {
            const RoomNode& n = graph.nodes[i];
            RoomTemplate tpl = SelectTemplate(rng, n.type);

            // 部屋セルの左下（実タイル）。セル内でテンプレを中央寄せ。
            int cellOriginI = margin + (n.rx - graph.minRX) * cellW;
            int cellOriginJ = margin + (n.ry - graph.minRY) * cellH;
            int offI = cellOriginI + (p.cellTilesW - tpl.width) / 2;
            int offJ = cellOriginJ + (p.cellTilesH - tpl.height) / 2;

            PlacedRoom pr;
            pr.type = n.type;
            pr.tpl = tpl;
            pr.offI = offI;
            pr.offJ = offJ;
            pr.nodeIdx = i;
            ctx.placed[i] = pr;
        }
    }

    //========================================================================
    //  焼き込み + スポーン絶対座標化
    //========================================================================
    static void StampRooms(GenContext& ctx, DungeonGenerateResult& out)
    {
        out.width = ctx.gridW;
        out.depth = ctx.gridH;
        out.solid.assign(out.width, std::vector<bool>(out.depth, true)); // 初期は全部壁

        for (auto& pr : ctx.placed)
        {
            const RoomTemplate& tpl = pr.tpl;
            for (int x = 0; x < tpl.width; ++x)
                for (int z = 0; z < tpl.height; ++z)
                {
                    int gi = pr.offI + x, gj = pr.offJ + z;
                    if (gi < 0 || gj < 0 || gi >= out.width || gj >= out.depth) continue;
                    if (x < (int)tpl.solidMask.size() && z < (int)tpl.solidMask[x].size())
                        out.solid[gi][gj] = tpl.solidMask[x][z];
                }

            for (auto& m : tpl.spawnMarkers)
                pr.worldSpawns.push_back({ { pr.offI + m.cell.i, pr.offJ + m.cell.j }, m.type });
        }
    }

    //========================================================================
    //  フェーズD: 隣接部屋を通路で接続
    //========================================================================
    static void CarveRect(DungeonGenerateResult& out, int i0, int j0, int i1, int j1)
    {
        if (i0 > i1) std::swap(i0, i1);
        if (j0 > j1) std::swap(j0, j1);
        for (int i = i0; i <= i1; ++i)
            for (int j = j0; j <= j1; ++j)
                if (i >= 0 && j >= 0 && i < out.width && j < out.depth)
                    out.solid[i][j] = false;
    }

    static void ConnectCorridors(GenContext& ctx, const RoomGraph& graph,
        DungeonGenerateResult& out, const DungeonGenParams& p)
    {
        int half = std::max(0, (p.corridorWidth - 1) / 2);
        int extra = (p.corridorWidth - 1) - half;

        auto carve = [&](CellPos a, CellPos b)
            {
                // L字: X 方向→Z 方向。幅を持たせる。
                CarveRect(out, a.i, a.j - half, b.i, a.j + extra);
                CarveRect(out, b.i - half, a.j, b.i + extra, b.j);
            };

        // グラフの各エッジ（重複を避けるため i<nb のみ）を接続
        for (int i = 0; i < (int)graph.nodes.size(); ++i)
            for (int nb : graph.nodes[i].neighbors)
                if (i < nb)
                    carve(RoomCenter(ctx.placed[i]), RoomCenter(ctx.placed[nb]));
    }

    //========================================================================
    //  メタ収集（部屋情報 + Start/Portal セル）
    //========================================================================
    static void CollectMeta(GenContext& ctx, const RoomGraph& graph, DungeonGenerateResult& out)
    {
        for (auto& pr : ctx.placed)
        {
            RoomMeta meta;
            meta.type = pr.type;
            meta.minI = pr.offI;
            meta.minJ = pr.offJ;
            meta.maxI = pr.offI + pr.tpl.width - 1;
            meta.maxJ = pr.offJ + pr.tpl.height - 1;
            meta.spawnMarkers = pr.worldSpawns;
            out.rooms.push_back(meta);
        }

        // Start / Portal セルはグラフのノードから直接決める（確実）
        out.startCell = RoomCenter(ctx.placed[graph.startIdx]);

        // Portal 型のノードを探す。無ければゴールノード。
        int portalNode = -1;
        for (int i = 0; i < (int)graph.nodes.size(); ++i)
            if (graph.nodes[i].type == RoomType::Portal) { portalNode = i; break; }
        if (portalNode < 0) portalNode = graph.goalIdx;
        out.portalCell = RoomCenter(ctx.placed[portalNode]);

        // スポーンマーカーに Start/Portal を必ず1つ入れておく（呼び出し側が使う）
        bool hasStart = false, hasPortal = false;
        for (auto& room : out.rooms)
            for (auto& m : room.spawnMarkers)
            {
                if (m.type == SpawnMarkerType::StartPoint) hasStart = true;
                if (m.type == SpawnMarkerType::Portal)     hasPortal = true;
            }
        if (!hasStart && !out.rooms.empty())
            out.rooms[graph.startIdx].spawnMarkers.push_back({ out.startCell, SpawnMarkerType::StartPoint });
        if (!hasPortal && portalNode >= 0)
            out.rooms[portalNode].spawnMarkers.push_back({ out.portalCell, SpawnMarkerType::Portal });
    }

    //========================================================================
    //  Generate 本体
    //========================================================================
    DungeonGenerateResult DungeonGenerator::Generate(uint32_t seed, int floorIndex)
    {
        return Generate(seed, floorIndex, DungeonGenParams{});
    }

    DungeonGenerateResult DungeonGenerator::Generate(uint32_t seed, int floorIndex,
        const DungeonGenParams& params)
    {
        floorIndex = std::clamp(floorIndex, 1, 5);

        DungeonGenerateResult out;
        out.usedSeed = seed;
        out.cellSize = 2.0f;
        out.originX = 0.0f;
        out.originZ = 0.0f;

        uint64_t mixed = ((uint64_t)seed << 8) ^ (uint64_t)(floorIndex * 0x9E37u + 1);
        Rng rng(mixed);

        RoomGraph graph = BuildRoomGraph(rng, floorIndex, params);

        GenContext ctx;
        PlaceRooms(rng, graph, params, ctx);
        StampRooms(ctx, out);
        ConnectCorridors(ctx, graph, out, params);
        CollectMeta(ctx, graph, out);

        return out;
    }

    //========================================================================
    //  ASCII アート
    //========================================================================
    std::string DungeonGenerator::BuildAsciiArt(const DungeonGenerateResult& result)
    {
        std::string s;
        s.reserve((size_t)(result.width + 1) * result.depth);

        auto charAt = [&](int i, int j) -> char
            {
                if (i == result.startCell.i && j == result.startCell.j)  return 'S';
                if (i == result.portalCell.i && j == result.portalCell.j) return 'P';
                return result.solid[i][j] ? '#' : ' ';
            };

        for (int j = result.depth - 1; j >= 0; --j)
        {
            for (int i = 0; i < result.width; ++i)
                s.push_back(charAt(i, j));
            s.push_back('\n');
        }
        return s;
    }

    void DungeonGenerator::DebugPrintGrid(const DungeonGenerateResult& result)
    {
        std::string art = BuildAsciiArt(result);
#if defined(RUNEFORGE_HAS_LOGGER)
        CCL_LOG_INFO(LogCategory::Core,
            "[Dungeon] seed=%u  size=%dx%d  rooms=%zu\n%s",
            result.usedSeed, result.width, result.depth,
            result.rooms.size(), art.c_str());
#else
        std::printf("[Dungeon] seed=%u  size=%dx%d  rooms=%zu\n",
            result.usedSeed, result.width, result.depth, result.rooms.size());
        std::printf("%s", art.c_str());
#endif
    }

} // namespace RuneForge::Dungeon