#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/AI/BehaviorTreeEditorCommon.h"

using namespace BTEditor;

void BehaviorTreeWindow::DrawOutliner(const BehaviorTreeComponent *activeAI)
{
    // キャンバスは木が横へ伸びるほど全体が読めなくなるので、文字の階層で全体を見せる場所を持つ。

    // 一覧は自分ではデータを持たず、RebuildHierarchy が毎フレーム作る親子関係
    // （m_roots / m_childrenOf）をなぞって描くだけ。だからリンクを繋ぎ替えれば次のフレームで一覧も変わる。

    // 表（Table）の中に木を入れる。種類を名前と別の列に出すため。
    // 名前の頭に付けると、字下げの深さで種類の位置がずれて縦に読めない。
    // 1行おきの背景の塗り（RowBg）は、見にくいと本人が判断したので使わない
    ZoneScopedN("BTW: Outliner");

    ImGui::TextUnformatted("ヒエラルキー (Hierarchy)");
    ImGui::Separator();

    // ScrollY：一覧だけをスクロールさせる（見出しは固定したまま）
    // BordersInnerV：名前と種類の列の境目に縦線を引く
    const ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY;

    // 行の上下に少し余白を足して、行と行の境目を分かりやすくする
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 3.0f));
    if (ImGui::BeginTable("##BTHierarchy", 2, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1); // 列の見出しはスクロールしても残す
        ImGui::TableSetupColumn("名前", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("種類", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();

        // 根から1行ずつ描く。子は DrawOutlinerRow が再帰で描く
        std::unordered_set<uint32_t> visited;
        for (uint32_t root : m_roots)
        {
            DrawOutlinerRow(root, activeAI, visited);
        }
        ZoneValue(visited.size()); // 描いた行数

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
}

void BehaviorTreeWindow::DrawOutlinerRow(uint32_t nodeId, const BehaviorTreeComponent *activeAI, std::unordered_set<uint32_t> &visited)
{
    // 処理フロー:
    // 1. このノードの情報を集める
    // 2. 名前の列の部品1：矢印と押せる範囲（TreeNodeEx）
    // 3. クリックでキャンバスをそのノードへ寄せる
    // 4. 名前の列の部品2：種類の色の四角
    // 5. 名前の列の部品3：名前
    // 6. 種類の列
    // 7. 子の行（再帰）

    // 【備考】
    //   名前の列は左から3つの部品でできている:
    //
    //     [▼ 矢印と押せる範囲]  [■ 種類の色]  [名前]
    //      TreeNodeEx(文字は空)   Dummy＋塗り    TextColored
    //
    //   TreeNodeEx は文字を矢印のすぐ右に描くので、名前まで任せると間に色の四角を挟めない。
    //   だから文字は空にして、四角と名前は SameLine（改行せず右隣へ）で後から並べる。

    // ---------------------------------------------------------
    // 1. 準備
    // ---------------------------------------------------------

    // 同じノードを2回描かない。リンクの付け間違いで輪になっていても、無限に潜り続けないため。
    if (!visited.insert(nodeId).second) return;

    const EditorBTNode *node = FindNode(nodeId);
    if (!node) return;

    auto       children    = m_childrenOf.find(nodeId);
    const bool hasChildren = (children != m_childrenOf.end() && !children->second.empty());

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    // ---------------------------------------------------------
    // 2. 矢印と押せる範囲
    // ---------------------------------------------------------

    // OpenOnArrow     : 矢印を押したときだけ開閉する（行を押すのは「移動」に使うため）
    // DefaultOpen     : 最初は全部開いた状態で出す
    // SpanAllColumns  : 種類の列まで含めた行全体を押せる範囲にする
    // DrawLinesToNodes: 親から子へ線を引く（どの行がどの親の子かを、字下げより確かに見せる）
    // Leaf            : 子が無い行は矢印を出さない
    // Selected        : キャンバスで選択中のノードの行を塗る
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (ed::IsNodeSelected(nodeId)) flags |= ImGuiTreeNodeFlags_Selected;

    // 行の ID にはノードIDを渡す（第1引数）。ImGui はこの ID で開閉状態を覚える。
    // 名前は「セレクター」が何個もあるように重複しうるので、ID には使えない。
    const bool open = ImGui::TreeNodeEx(reinterpret_cast<void *>(static_cast<uintptr_t>(nodeId)), flags, "");

    // ---------------------------------------------------------
    // 3. クリック
    // ---------------------------------------------------------

    // IsItemClicked は「直前に作った部品」を見るので、TreeNodeEx の直後に置く

    // IsItemToggledOpen を除くのは、矢印を押した（開閉した）ときまで移動しないため。
    // 倍率は変えない（false）。0.3 秒かけて滑らかに動かす。
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
    {
        ed::ClearSelection();
        ed::SelectNode(nodeId);
        ed::NavigateToSelection(false, 0.3f);
    }

    // ---------------------------------------------------------
    // 4. 種類の色
    // ---------------------------------------------------------

    // キャンバスの見出しと同じ色

    // Dummy は「何も描かない箱」。先に場所を取っておかないと、次の名前が四角に重なる。
    ImGui::SameLine();
    const float  lineH   = ImGui::GetTextLineHeight();
    const float  size    = lineH * 0.7f; // 文字より少し小さい正方形
    const ImVec2 topLeft = ImGui::GetCursorScreenPos();
    const ImVec2 boxMin  = ImVec2(topLeft.x, topLeft.y + (lineH - size) * 0.5f); // 行の高さの中央へ
    const ImVec2 boxMax  = ImVec2(boxMin.x + size, boxMin.y + size);
    ImGui::Dummy(ImVec2(size, lineH));

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(boxMin, boxMax, HeaderColorOf(node->type), 2.0f);
    drawList->AddRect(boxMin, boxMax, ImColor(150, 150, 150, 180), 2.0f); // ROOT の暗い灰色が背景に溶けないよう縁を付ける

    // ---------------------------------------------------------
    // 5. 名前
    // ---------------------------------------------------------

    // 実行中は黄色（キャンバスの枠の点滅と同じ情報）
    ImVec4 nameColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (DebugStateOf(*node, activeAI) == BTDebugState::Running)
        nameColor = ImVec4(1.0f, 0.85f, 0.2f, 1.0f);

    ImGui::SameLine();
    // まとまりは付けた名前、Action / Condition / Decorator は中身から作った名前（キャンバスの見出しと同じ）
    ImGui::TextColored(nameColor, "%s", GetDisplayName(*node).c_str());

    // ---------------------------------------------------------
    // 6. 種類
    // ---------------------------------------------------------

    // 種類の色で書く（左の四角と同じ色。名前より目立たないよう、少し明るくして読みやすくする）
    ImGui::TableNextColumn();
    const ImVec4 typeColor = HeaderColorOf(node->type).Value;
    ImGui::TextColored(ImVec4(typeColor.x * 0.5f + 0.5f, typeColor.y * 0.5f + 0.5f, typeColor.z * 0.5f + 0.5f, 1.0f),
                       "%s", TypeNameOf(node->type));

    // ---------------------------------------------------------
    // 7. 子の行
    // ---------------------------------------------------------

    // TreeNodeEx が true（開いている）なら子を描き、最後に TreePop で字下げを1段戻す
    if (open)
    {
        if (hasChildren)
        {
            for (uint32_t child : children->second)
                DrawOutlinerRow(child, activeAI, visited);
        }
        ImGui::TreePop();
    }
}
