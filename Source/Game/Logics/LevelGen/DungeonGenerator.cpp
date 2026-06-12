//============================================================================
//  DungeonGenerator.cpp
//  フェーズ A: グラフ生成 / B: テンプレート選択 / C: 物理配置 / D: 通路接続
//============================================================================
#include "DungeonGenerator.h"
#include "RoomTemplateTable.h"

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <string>
#include <vector>

// Logger は任意。プロジェクト側に存在すれば使い、無ければ printf にフォールバック。
#if defined(RUNEFORGE_HAS_LOGGER)
#include "Engine/Platform/Core/Logger.h"
#endif

namespace RuneForge::Dungeon
{
    //========================================================================
    //  決定的乱数 (splitmix64 ベース)。同一シードで完全再現。
    //========================================================================
    class Rng
    {
    public:
        explicit Rng(uint64_t seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

        uint32_t NextU32()
        {
            // splitmix64
            uint64_t z = (_state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z = z ^ (z >> 31);
            return (uint32_t)(z >> 32);
        }
        // [0, n)
        int RangeInt(int n)
        {
            if (n <= 0) return 0;
            return (int)(NextU32() % (uint32_t)n);
        }
        // [lo, hi]
        int RangeInt(int lo, int hi)
        {
            if (hi <= lo) return lo;
            return lo + RangeInt(hi - lo + 1);
        }
        template<typename T>
        const T& Pick(const std::vector<T>& v) { return v[RangeInt((int)v.size())]; }

    private:
        uint64_t _state;
    };

    //========================================================================
    //  内部: 配置済みの部屋（テンプレートをグリッド上に置いた結果）
    //========================================================================
    struct PlacedRoom
    {
        RoomType     type;
        int          offI = 0, offJ = 0;       // グリッド上の左下オフセット
        RoomTemplate tpl;                       // 回転適用後のテンプレート
        int          graphDepth = 0;            // スタートからのグラフ距離
        std::vector<DoorInfo> worldDoors;       // グリッド絶対座標のドア
        std::vector<SpawnMarker> worldSpawns;   // グリッド絶対座標のスポーン

        int Width()  const { return tpl.width; }
        int Height() const { return tpl.height; }
    };

    //========================================================================
    //  フェーズA: 部屋グラフ（必須部屋を必ず含む線形＋分岐の系列）
    //
    //  各層を「スタート → 戦闘群 → ... → ポータル（最深部）」の主系列とし、
    //  ショップ/鍛冶/宝物庫/エリートを分岐として差し込む。
    //  第5層はポータルの前にボス部屋を最深部として置く。
    //========================================================================
    struct GraphNode
    {
        RoomType type;
        int      depth;   // 主系列上の深さ（大きいほど奥）
        bool     onMainPath; // true=主系列、false=分岐（袋小路）
        int      parent;  // 親ノードのインデックス（-1=ルート）
    };

    static std::vector<GraphNode> BuildRoomGraph(Rng& rng, int floorIndex)
    {
        std::vector<GraphNode> g;

        auto add = [&](RoomType t, int depth, bool main, int parent) -> int
            {
                g.push_back({ t, depth, main, parent });
                return (int)g.size() - 1;
            };

        // --- 主系列 ---
        int prev = add(RoomType::Start, 0, true, -1);  // depth0: スタート
        int depth = 1;

        // 戦闘部屋の数（層が深いほど多め）
        int combatCount = 2 + floorIndex; // 層1で3, 層5で7
        // 主系列上の戦闘部屋。分岐は途中の戦闘部屋にぶら下げる。
        std::vector<int> mainCombat;
        for (int c = 0; c < combatCount; ++c)
        {
            int node = add(RoomType::Combat, depth, true, prev);
            mainCombat.push_back(node);
            prev = node;
            depth++;
        }

        // --- 必須分岐部屋を主系列の戦闘部屋にぶら下げる ---
        auto attachBranch = [&](RoomType t)
            {
                if (mainCombat.empty()) { add(t, depth, false, 0); return; }
                int host = mainCombat[rng.RangeInt((int)mainCombat.size())];
                add(t, g[host].depth, false, host);
            };

        attachBranch(RoomType::Shop);      // ショップ x1
        attachBranch(RoomType::Forge);     // 鍛冶 x1
        attachBranch(RoomType::Treasury);  // 宝物庫 x1
        if (rng.RangeInt(2) == 0)          // 宝物庫は1〜2
            attachBranch(RoomType::Treasury);
        attachBranch(RoomType::Elite);     // エリート x1

        // --- 第5層: ボス部屋を最深部に。その後ポータル ---
        if (floorIndex >= 5)
        {
            int boss = add(RoomType::Boss, depth, true, prev);
            prev = boss;
            depth++;
        }

        // --- ポータル部屋（最深部）---
        add(RoomType::Portal, depth, true, prev);

        return g;
    }

    //========================================================================
    //  フェーズB: 各グラフノードにテンプレートを割り当て（回転込み）
    //========================================================================
    static RoomTemplate SelectTemplate(Rng& rng, RoomType type)
    {
        std::vector<int> cands = GetTemplatesForType(type);
        const auto& table = GetRoomTemplateTable();
        const RoomTemplate& base =
            cands.empty() ? table[0] : table[cands[rng.RangeInt((int)cands.size())]];

        // 許可された回転からランダムに選ぶ
        int rot = 0;
        if (!base.allowedRotations.empty())
            rot = base.allowedRotations[rng.RangeInt((int)base.allowedRotations.size())];
        return RotateTemplate(base, rot);
    }

    //========================================================================
    //  フェーズC: 物理配置
    //
    //  単純で堅牢な「行レイアウト」：主系列を Z+ 方向に積み、各深さで
    //  X 方向に部屋を並べる。部屋同士・通路に余白(gap)を取り重なりを防ぐ。
    //  これにより「重ならない」「ポータルが最深部(Z最大)」を保証する。
    //========================================================================
    struct GenContext
    {
        std::vector<PlacedRoom> placed;
        int gridW = 0;
        int gridH = 0;
    };

    static void PlaceRooms(Rng& rng, const std::vector<GraphNode>& graph, GenContext& ctx)
    {
        const int margin = 2;       // 外周マージン
        const int rowGap = 4;       // 行間（Z方向の通路余白）
        const int colGap = 4;       // 列間（X方向の通路余白）

        // 深さごとにノードをまとめる
        int maxDepth = 0;
        for (auto& n : graph) maxDepth = std::max(maxDepth, n.depth);

        std::vector<std::vector<int>> byDepth(maxDepth + 1);
        for (int i = 0; i < (int)graph.size(); ++i)
            byDepth[graph[i].depth].push_back(i);

        // まず各ノードのテンプレートを確定（回転込み）。インデックス対応で保持。
        std::vector<RoomTemplate> tpls(graph.size());
        for (int i = 0; i < (int)graph.size(); ++i)
            tpls[i] = SelectTemplate(rng, graph[i].type);

        // 行ごとの高さ（Z方向）と、その行の開始 J を求める
        int curJ = margin;
        int maxI = 0;

        // 後で絶対 J を決めるため、行高さを先に算出
        for (int d = 0; d <= maxDepth; ++d)
        {
            auto& nodes = byDepth[d];
            if (nodes.empty()) continue;

            int rowHeight = 0;
            for (int idx : nodes) rowHeight = std::max(rowHeight, tpls[idx].height);

            // この行を X 方向に並べる
            int curI = margin;
            for (int idx : nodes)
            {
                PlacedRoom pr;
                pr.type = graph[idx].type;
                pr.tpl = tpls[idx];
                pr.graphDepth = graph[idx].depth;
                pr.offI = curI;
                // 行内で縦センタリング（見栄え＆通路接続のため上下中央寄せ）
                pr.offJ = curJ + (rowHeight - pr.tpl.height) / 2;

                ctx.placed.push_back(pr);
                curI += pr.tpl.width + colGap;
            }
            maxI = std::max(maxI, curI - colGap + margin);
            curJ += rowHeight + rowGap;
        }

        ctx.gridW = std::max(maxI, margin * 2 + 1);
        ctx.gridH = curJ - rowGap + margin;
    }

    //========================================================================
    //  グリッドへ焼き込み + ドア/スポーンの絶対座標化
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
                    int gi = pr.offI + x;
                    int gj = pr.offJ + z;
                    if (gi < 0 || gj < 0 || gi >= out.width || gj >= out.depth) continue;
                    out.solid[gi][gj] = tpl.solidMask[x][z];
                }

            // ドア絶対座標
            for (auto& d : tpl.doors)
                pr.worldDoors.push_back({ { pr.offI + d.cell.i, pr.offJ + d.cell.j }, d.direction });
            // スポーン絶対座標
            for (auto& m : tpl.spawnMarkers)
                pr.worldSpawns.push_back({ { pr.offI + m.cell.i, pr.offJ + m.cell.j }, m.type });
        }
    }

    //========================================================================
    //  フェーズD: 通路接続
    //   行レイアウトなので「深さ d の各部屋」から「深さ d+1 の部屋」へ
    //   L字通路を引く。主系列が必ず繋がるようにし、分岐もホストへ繋ぐ。
    //   通路幅 corridorWidth をパラメータ化。
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

    static CellPos RoomCenter(const PlacedRoom& pr)
    {
        return { pr.offI + pr.tpl.width / 2, pr.offJ + pr.tpl.height / 2 };
    }

    static void ConnectCorridors(GenContext& ctx, DungeonGenerateResult& out,
        const std::vector<GraphNode>& graph, int corridorWidth)
    {
        int half = std::max(0, (corridorWidth - 1) / 2);
        int extra = (corridorWidth - 1) - half; // 偶数幅対応

        // placed[i] は graph[i] に1:1対応（PlaceRooms で同順 push の前提が崩れるため、
        // ここでは graphDepth + 親子関係ではなく「親ノードのcenter」へ引く方式に統一する）。
        // placed と graph の対応を作る:
        // PlaceRooms は byDepth 順で push しているため index がずれる。
        // よって ctx.placed をタイプ/深さで graph と突き合わせるのではなく、
        // 「中心座標どうしを親子で結ぶ」ために graph 順の placed を別途構築する。

        // graph 順に並んだ placed への参照表を作る。
        // PlaceRooms と同じ走査順序を再現して index を引き当てる。
        int maxDepth = 0;
        for (auto& n : graph) maxDepth = std::max(maxDepth, n.depth);
        std::vector<std::vector<int>> byDepth(maxDepth + 1);
        for (int i = 0; i < (int)graph.size(); ++i)
            byDepth[graph[i].depth].push_back(i);

        std::vector<int> placedOfGraph(graph.size(), -1);
        int p = 0;
        for (int d = 0; d <= maxDepth; ++d)
            for (int gIdx : byDepth[d])
                placedOfGraph[gIdx] = p++;

        auto carveCorridor = [&](CellPos a, CellPos b)
            {
                // L字: まず X 方向、その後 Z 方向。幅を持たせる。
                int i0 = a.i, j0 = a.j, i1 = b.i, j1 = b.j;
                // 水平セグメント (j0 固定)
                CarveRect(out, i0, j0 - half, i1, j0 + extra);
                // 垂直セグメント (i1 固定)
                CarveRect(out, i1 - half, j0, i1 + extra, j1);
            };

        // 親子をすべて接続（主系列・分岐ともに）
        for (int gi = 0; gi < (int)graph.size(); ++gi)
        {
            int parent = graph[gi].parent;
            if (parent < 0) continue;
            int pa = placedOfGraph[parent];
            int pb = placedOfGraph[gi];
            if (pa < 0 || pb < 0) continue;
            carveCorridor(RoomCenter(ctx.placed[pa]), RoomCenter(ctx.placed[pb]));
        }
    }

    //========================================================================
    //  ルームメタ情報 / グローバル情報の収集
    //========================================================================
    static void CollectMeta(GenContext& ctx, DungeonGenerateResult& out)
    {
        bool startSet = false, portalSet = false;

        for (auto& pr : ctx.placed)
        {
            RoomMeta meta;
            meta.type = pr.type;
            meta.minI = pr.offI;
            meta.minJ = pr.offJ;
            meta.maxI = pr.offI + pr.tpl.width - 1;
            meta.maxJ = pr.offJ + pr.tpl.height - 1;
            meta.doors = pr.worldDoors;
            meta.spawnMarkers = pr.worldSpawns;
            out.rooms.push_back(meta);

            // グローバル情報
            for (auto& m : pr.worldSpawns)
            {
                if (m.type == SpawnMarkerType::StartPoint && !startSet)
                {
                    out.startCell = m.cell; startSet = true;
                }
                if (m.type == SpawnMarkerType::Portal && !portalSet)
                {
                    out.portalCell = m.cell; portalSet = true;
                }
            }
        }

        // フォールバック: スタート/ポータルが取れない場合は部屋中心で代用
        if (!startSet && !ctx.placed.empty())
            out.startCell = RoomCenter(ctx.placed.front());
        if (!portalSet && !ctx.placed.empty())
            out.portalCell = RoomCenter(ctx.placed.back());
    }

    //========================================================================
    //  Generate 本体
    //========================================================================
    DungeonGenerateResult DungeonGenerator::Generate(uint32_t seed, int floorIndex)
    {
        floorIndex = std::clamp(floorIndex, 1, 5);

        DungeonGenerateResult out;
        out.usedSeed = seed;
        out.cellSize = 2.0f;
        out.originX = 0.0f;
        out.originZ = 0.0f;

        // シードと層番号を混ぜて層ごとに別レイアウトにする
        uint64_t mixed = ((uint64_t)seed << 8) ^ (uint64_t)(floorIndex * 0x9E37u + 1);
        Rng rng(mixed);

        // 通路幅（推奨 2〜3）
        const int corridorWidth = 3;

        // A: グラフ
        std::vector<GraphNode> graph = BuildRoomGraph(rng, floorIndex);

        // C: 物理配置（B のテンプレート選択は PlaceRooms 内で実施）
        GenContext ctx;
        PlaceRooms(rng, graph, ctx);

        // 焼き込み
        StampRooms(ctx, out);

        // D: 通路
        ConnectCorridors(ctx, out, graph, corridorWidth);

        // メタ収集
        CollectMeta(ctx, out);

        return out;
    }

    //========================================================================
    //  ASCII アート
    //========================================================================
    std::string DungeonGenerator::BuildAsciiArt(const DungeonGenerateResult& result)
    {
        // 行 = j（Z）、列 = i（X）。Z+ が上に来るように上から下へ j=depth-1..0 で描く。
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