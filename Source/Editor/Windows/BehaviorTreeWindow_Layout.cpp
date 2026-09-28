// BehaviorTreeWindow：自動整列とコメント枠の配置。
//
// 整列が実行順を変えない理由:
//   子の実行順は兄弟の X の左右で決まる。整列は各親の子を「今の実行順」のまま左から詰め直す
//   だけなので、相対順序は必ず保たれる。
//
// コメント枠の座標の単位:
//   ライブラリはコメント枠を「ノード全体＝余白＋見出し帯＋枠」で持つ。width/height は枠の寸法、
//   posX/posY はノード全体の左上なので、囲みたい矩形から逆算するのに GroupFrameOffset/Extra を使う。
#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Engine/Platform/Core/Logger.h"
#include <algorithm>
#include <cfloat>
#include <functional>

using namespace BTEditor;

void BehaviorTreeWindow::AutoLayout()
{
    if (m_nodes.empty()) return;

    RecordStateForUndo("Auto Layout");

    // 親子・隠れているノードを最新にする。1. のグループ判定も隠れたノードを除くのでここで先に作る。
    RebuildHierarchy();

    // IDからノードを引く表。毎回 find_if すると O(N^2) になる。
    std::unordered_map<uint32_t, EditorBTNode *> byId;
    for (auto &n : m_nodes)
        byId[n.id] = &n;

    auto find = [&](uint32_t id) -> EditorBTNode * {
        auto it = byId.find(id);
        return (it == byId.end()) ? nullptr : it->second;
    };

    // 実サイズ。まだ描かれていないノードは仮値で置く。
    auto sizeOf = [&](uint32_t id) {
        const ImVec2 s = ed::GetNodeSize(id);
        return ImVec2((s.x > 1.0f) ? s.x : kLayoutDefaultW,
                      (s.y > 1.0f) ? s.y : kLayoutDefaultH);
    };

    // ---------------------------------------------------------------
    // 1. グループが今どのノードを囲んでいるかを控える
    //    ノードを動かした後では判定できないので、必ず動かす前に取る。
    // ---------------------------------------------------------------
    struct GroupSnapshot
    {
        uint32_t              id;
        std::vector<uint32_t> members;
    };
    std::vector<GroupSnapshot> groups;
    for (const auto &n : m_nodes)
        if (n.type == EditorBTNodeType::Group && n.graphId == CurrentGraphId())
            groups.push_back({n.id, CollectNodesInGroup(n.id)});

    // ---------------------------------------------------------------
    // 2. 親→子のリストを作る
    //    RebuildHierarchy が子を「今のX順（同じなら上が先）」に並べてある。
    //    ここが整列の安全性の要で、その順序のまま左から詰めるので、
    //    実行順は整列の前後で変わらない。
    // ---------------------------------------------------------------
    const auto &childrenOf = m_childrenOf;

    // ---------------------------------------------------------------
    // 3. 部分木の幅と深さを求める（葉から根へ）
    // ---------------------------------------------------------------
    std::unordered_map<uint32_t, float> widthOf;
    std::unordered_map<uint32_t, int>   depthOf;
    std::unordered_set<uint32_t>        visited;

    std::function<float(uint32_t, int)> measure = [&](uint32_t id, int depth) -> float {
        // 循環リンクや、1つの子に複数の親が繋がった形でも止まるようにする。
        if (!visited.insert(id).second) return 0.0f;

        depthOf[id]     = depth;
        const float own = sizeOf(id).x;

        auto it = childrenOf.find(id);
        if (it == childrenOf.end() || it->second.empty())
        {
            widthOf[id] = own;
            return own;
        }

        float sum = 0.0f;
        for (uint32_t c : it->second)
        {
            const float w = measure(c, depth + 1);
            if (w <= 0.0f) continue; // 既に別経路で置かれた子は幅を数えない
            if (sum > 0.0f) sum += kLayoutHGap;
            sum += w;
        }

        widthOf[id] = (std::max)(own, sum);
        return widthOf[id];
    };

    // 根＝親を持たないノード。Root 型が先頭（RebuildHierarchy で並べ済み）。
    //   並べるのは今見ている階層だけ（サブツリーの中身や外側は、その階層に入ったときに並べる）。
    std::vector<uint32_t> roots;
    for (uint32_t r : m_roots)
    {
        if (!m_hiddenNodes.count(r)) roots.push_back(r);
    }

    for (uint32_t r : roots)
        measure(r, 0);

    // どの根からも辿れなかったノード（リンクが循環している塊など）も置き去りにしない。
    //   別の階層のノードはその階層に入ったときに並べるので、ここでは拾わない。
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group) continue;
        if (m_hiddenNodes.count(n.id)) continue;
        if (visited.count(n.id)) continue;
        roots.push_back(n.id);
        measure(n.id, 0);
    }

    if (roots.empty()) return;

    // ---------------------------------------------------------------
    // 4. 段ごとの高さを揃える
    //    ノードは種類で高さが違う（Actionのゲージ付きが一番高い）。
    //    段の中の最大値で行の高さを決めると、線が斜めに交差しなくなる。
    // ---------------------------------------------------------------
    int maxDepth = 0;
    for (const auto &kv : depthOf)
        maxDepth = (std::max)(maxDepth, kv.second);

    std::vector<float> rowHeight(static_cast<size_t>(maxDepth) + 1, kLayoutDefaultH);
    for (const auto &kv : depthOf)
    {
        const float h = sizeOf(kv.first).y;
        float      &r = rowHeight[static_cast<size_t>(kv.second)];
        if (h > r) r = h;
    }

    std::vector<float> rowTop(rowHeight.size(), 0.0f);
    for (size_t d = 1; d < rowTop.size(); ++d)
        rowTop[d] = rowTop[d - 1] + rowHeight[d - 1] + kLayoutVGap;

    // ---------------------------------------------------------------
    // 5. 根から葉へ配置する
    // ---------------------------------------------------------------
    std::unordered_set<uint32_t> placed;

    std::function<void(uint32_t, float)> place = [&](uint32_t id, float left) {
        if (!placed.insert(id).second) return;

        EditorBTNode *n = find(id);
        if (!n) return;

        const ImVec2 sz = sizeOf(id);
        const float  w  = widthOf.count(id) ? widthOf[id] : sz.x;

        // 自分は割り当てられた領域の中央に置く。子の重心の上に来る。
        n->posX                = left + (w - sz.x) * 0.5f;
        n->posY                = rowTop[static_cast<size_t>(depthOf[id])];
        n->positionInitialized = false; // 描画ループが ed::SetNodePosition を呼ぶ

        auto it = childrenOf.find(id);
        if (it == childrenOf.end()) return;

        float cursor = left;
        for (uint32_t c : it->second)
        {
            if (placed.count(c)) continue;
            place(c, cursor);
            cursor += (widthOf.count(c) ? widthOf[c] : kLayoutDefaultW) + kLayoutHGap;
        }
    };

    float treeLeft = 0.0f;
    for (uint32_t r : roots)
    {
        if (placed.count(r)) continue;
        place(r, treeLeft);
        treeLeft += (widthOf.count(r) ? widthOf[r] : kLayoutDefaultW) + kLayoutTreeGap;
    }

    // ---------------------------------------------------------------
    // 6. グループ枠を、囲んでいたノードに合わせ直す
    //    これをやらないと、整列でノードだけが動いて枠が空のまま取り残される。
    // ---------------------------------------------------------------
    for (const auto &g : groups)
    {
        if (g.members.empty()) continue; // 空の枠は動かしようがないので触らない

        float                        minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
        int                          used = 0;
        std::unordered_set<uint32_t> counted;
        for (uint32_t m : g.members)
        {
            if (!counted.insert(m).second) continue;

            const EditorBTNode *n = find(m);
            if (!n || !placed.count(m)) continue;
            const ImVec2 s = sizeOf(m);
            minX           = (std::min)(minX, n->posX);
            minY           = (std::min)(minY, n->posY);
            maxX           = (std::max)(maxX, n->posX + s.x);
            maxY           = (std::max)(maxY, n->posY + s.y);
            ++used;
        }
        if (used == 0) continue;

        EditorBTNode *gn = find(g.id);
        if (!gn) continue;

        // width/height は「枠」、posX/posY は「ノード全体の左上」。
        // 単位が違うので GroupFrameOffset のぶんだけ左上へずらす。
        const ImVec2 off = GroupFrameOffset();
        gn->posX         = minX - kGroupPad - off.x;
        gn->posY         = minY - kGroupPad - off.y;
        gn->width        = (maxX - minX) + kGroupPad * 2.0f;
        gn->height       = (maxY - minY) + kGroupPad * 2.0f;
        // サイズの反映は positionInitialized=false に任せる（描画ループ側に集約済み）。
        gn->positionInitialized = false;
    }

    CCL_LOG_INFO(LogCategory::Editor, "[BT] 整列しました（%zu ノード / %zu グループ）",
                 placed.size(), groups.size());
}

std::vector<uint32_t> BehaviorTreeWindow::CollectNodesInGroup(uint32_t groupId) const
{
    std::vector<uint32_t> result;

    const ImVec2 gPos  = ed::GetNodePosition(groupId);
    const ImVec2 gSize = ed::GetNodeSize(groupId);
    if (gSize.x <= 0.0f || gSize.y <= 0.0f) return result; // まだ一度も描かれていない

    for (const auto &n : m_nodes)
    {
        if (n.id == groupId) continue;
        if (n.type == EditorBTNodeType::Group) continue; // 入れ子は今は扱わない

        // 別の階層のノードは、同じキャンバス座標にあっても別の画面にいるので判定しない
        if (m_hiddenNodes.count(n.id)) continue;

        const ImVec2 p  = ed::GetNodePosition(n.id);
        const ImVec2 s  = ed::GetNodeSize(n.id);
        const float  cx = p.x + s.x * 0.5f;
        const float  cy = p.y + s.y * 0.5f;

        if (cx >= gPos.x && cx <= gPos.x + gSize.x &&
            cy >= gPos.y && cy <= gPos.y + gSize.y)
            result.push_back(n.id);
    }
    return result;
}

void BehaviorTreeWindow::GroupSelectedNodes()
{
    std::vector<ed::NodeId> selected;
    selected.resize(ed::GetSelectedObjectCount());
    const int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
    if (count <= 0) return;

    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    int   used = 0;

    for (int i = 0; i < count; ++i)
    {
        const uint32_t id = ToEditorId(selected[i].Get());

        auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                               [&](const EditorBTNode &n) { return n.id == id; });
        if (it == m_nodes.end()) continue;
        if (it->type == EditorBTNodeType::Group) continue; // 枠で枠を囲まない

        const ImVec2 p = ed::GetNodePosition(id);
        const ImVec2 s = ed::GetNodeSize(id);
        if (s.x <= 1.0f || s.y <= 1.0f) continue;

        minX = (std::min)(minX, p.x);
        minY = (std::min)(minY, p.y);
        maxX = (std::max)(maxX, p.x + s.x);
        maxY = (std::max)(maxY, p.y + s.y);
        ++used;
    }

    if (used == 0)
    {
        CCL_LOG_WARN(LogCategory::Editor, "[BT] 囲めるノードが選択されていません");
        return;
    }

    RecordStateForUndo("Group Selected Nodes");

    // width/height は「枠」、座標は「ノード全体の左上」。単位を合わせる。
    const ImVec2 off = GroupFrameOffset();
    CreateNode(EditorBTNodeType::Group, "コメント",
               minX - kGroupPad - off.x, minY - kGroupPad - off.y);

    // サイズは描画ループが ed::SetGroupSize で反映する（CreateNode が
    // positionInitialized=false で作るため、必ずその分岐を通る）。
    EditorBTNode &g = m_nodes.back();
    g.width         = (maxX - minX) + kGroupPad * 2.0f;
    g.height        = (maxY - minY) + kGroupPad * 2.0f;
}

ImVec2 BehaviorTreeWindow::GroupFrameOffset()
{
    const ImVec4 pad = ed::GetStyle().NodePadding;
    return ImVec2(pad.x, pad.y + kGroupHeaderH + ImGui::GetStyle().ItemSpacing.y);
}

ImVec2 BehaviorTreeWindow::GroupFrameExtra()
{
    const ImVec4 pad = ed::GetStyle().NodePadding;
    const ImVec2 off = GroupFrameOffset();
    return ImVec2(off.x + pad.z, off.y + pad.w);
}
