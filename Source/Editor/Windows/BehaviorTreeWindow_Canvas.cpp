// BehaviorTreeWindow：キャンバス（ノード・リンクの描画と、マウス／キーの入力）。
//
// 1フレームの流れ（DrawNodeWorkspace）:
//   整列の予約を実行 → ノードを描く（畳まれて隠れたものは飛ばす）→ リンクを描く
//   → 自動追従・開閉 → リンク作成・削除・メニュー の順。
//   整列は ed::Begin の中でしかノードのサイズが取れないので、ここで処理する。
#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include <algorithm>
#include <cmath>

using namespace BTEditor;

void BehaviorTreeWindow::DrawNodeWorkspace(const BehaviorTreeComponent *activeAI)
{
    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50.0f || avail.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    ed::SetCurrentEditor(m_nodeContext);
    ed::Begin("My Node Editor");

    // 整列はここで実行する。ed::GetNodeSize が前フレームの実サイズを返すので、
    // 種類ごとの高さの違いを織り込んだ段組みができる。ed::Begin の外だと値が取れない。
    if (m_layoutRequested)
    {
        AutoLayout();
        m_layoutRequested = false;
    }

    // 開閉ボタンが押されたノード。描画ループ中に Undo 記録（m_nodes の全走査）を挟まないよう、後で処理する。
    uint32_t toggleCollapseId = 0;

    for (auto &node : m_nodes)
    {
        // 生成・読込・Undo・整列の直後は、座標（とコメント枠の大きさ）をライブラリへ流し込む。
        //   ed::Group(size) の引数は一度登録された後は無視されるので、大きさを外から変える口はここだけ。
        //   全経路がこの分岐を通るので、呼び忘れる経路が無い。
        if (!node.positionInitialized)
        {
            ed::SetNodePosition(node.id, ImVec2(node.posX, node.posY));
            if (node.type == EditorBTNodeType::Group)
                ed::SetGroupSize(node.id, ImVec2(node.width, node.height));
            node.positionInitialized = true;
        }

        // 畳まれた祖先を持つノードは描かない。描かなかったノードとピンはライブラリ側で
        // 「生きていない」扱いになり、選択・接続・当たり判定から外れる。座標は上で預けてあるので
        // 保存時の GetNodePosition も正しく返る。
        if (m_hiddenNodes.count(node.id)) continue;

        if (node.type == EditorBTNodeType::Group)
            DrawGroupNode(node);
        else if (DrawTreeNode(node, activeAI))
            toggleCollapseId = node.id;

        // ユーザーのドラッグを手元の座標へ写す（実行順の判定はこの値を使う）
        const ImVec2 p = ed::GetNodePosition(node.id);
        node.posX      = p.x;
        node.posY      = p.y;
    }

    DrawLinks(activeAI);

    if (m_autoFollow)
        FollowRunningNode(activeAI);
    else
        m_lastTrackedNodeId = 0;

    // 折りたたみの開閉（見出しの矢印、またはノードのダブルクリック）
    if (toggleCollapseId == 0)
    {
        const uint32_t dbl = ToEditorId(ed::GetDoubleClickedNode().Get());
        const EditorBTNode *n = (dbl != 0) ? FindNode(dbl) : nullptr;
        if (n && CanCollapse(*n) && HasChildren(dbl)) toggleCollapseId = dbl;
    }
    if (toggleCollapseId != 0)
    {
        if (const EditorBTNode *n = FindNode(toggleCollapseId))
            SetCollapsed(toggleCollapseId, !n->collapsed);
    }

    HandleLinkCreation();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelectedNodes();
        if (ImGui::IsKeyPressed(ImGuiKey_G)) GroupSelectedNodes();
    }

    HandleDeletion();
    HandleContextMenu();
    FitInitialView();

    ed::End();
}

// ====================================================================
// ノードの描画
// ====================================================================
void BehaviorTreeWindow::DrawGroupNode(EditorBTNode &node)
{
    // 選択中はオレンジで光らせ、それ以外は役割ごとの色。塗り分けておくと、
    // ズームアウトしたままでも「どの塊の話か」が読める。
    const GroupPalette &palette     = GroupPaletteAt(node.groupColorIndex);
    const bool          isSelected  = ed::IsNodeSelected(node.id);
    const ImColor       borderColor = isSelected ? ImColor(255, 165, 0, 255) : ImColor(palette.border);
    const ImColor       headerColor = isSelected ? ImColor(150, 95, 20, 245) : ImColor(palette.header);

    // ノード背景は透明にする。コメント枠もノードなので、まず全面に StyleColor_NodeBg が敷かれ、
    // その上に薄い枠色を重ねてもほとんど色が出ない。
    // 色は BeginNode の中で焼き込まれるので、Push は必ず BeginNode より前に置く。
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
    ed::PushStyleColor(ed::StyleColor_GroupBg, ImColor(palette.bg));
    ed::PushStyleColor(ed::StyleColor_GroupBorder, borderColor);
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 8.0f);
    ed::PushStyleVar(ed::StyleVar_GroupBorderWidth, isSelected ? 3.0f : 2.0f);

    ed::BeginNode(node.id);
    ImGui::PushID(static_cast<int>(node.id));

    const ImVec2 labelScreenPos = ImGui::GetCursorScreenPos();

    // 見出し帯は「高さだけ持つ」Dummy で場所を取る。
    //   ライブラリは「ノード上端から枠の上端まで」を掴める帯とみなし、枠の上端はこの Dummy の
    //   下端で決まる。高さは帯の描画高と揃えること（縮めると掴める範囲も縮む）。
    //   幅を 1px にするのは、幅がノード幅へ反映されて毎フレーム膨張するのを避けるため。
    ImGui::Dummy(ImVec2(1.0f, kGroupHeaderH));

    ImDrawList  *drawList   = ImGui::GetWindowDrawList();
    const ImVec4 nodePad    = ed::GetStyle().NodePadding;
    const ImVec2 nodeMin    = ImVec2(labelScreenPos.x - nodePad.x, labelScreenPos.y - nodePad.y);
    const float  bandBottom = labelScreenPos.y + kGroupHeaderH + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 headerMax  = ImVec2(nodeMin.x + node.width + GroupFrameExtra().x, bandBottom);

    drawList->AddRectFilled(nodeMin, headerMax, headerColor, 8.0f, ImDrawFlags_RoundCornersTop);
    drawList->AddLine(ImVec2(nodeMin.x, headerMax.y), headerMax, ImColor(0, 0, 0, 150), 2.0f);

    // 引いて見ているときは見出しの文字を拡大する（章立てとして読めるように）。
    //   zoom が 0 に近いと割り算が発散するので、下限と上限の両方で抑える。
    const float zoom = ed::GetCurrentZoom();
    if (zoom < 0.6f)
    {
        const float fontSize = (std::min)(64.0f, ImGui::GetFontSize() / (std::max)(0.01f, zoom) * 0.8f);
        drawList->AddText(ImGui::GetFont(), fontSize, labelScreenPos, ImColor(235, 235, 235, 255), node.name.c_str());
    }
    else
    {
        drawList->AddText(ImVec2(nodeMin.x + 10.0f, labelScreenPos.y + 4.0f), ImColor(240, 240, 240, 255), node.name.c_str());
    }

    ed::Group(ImVec2(node.width, node.height)); // リサイズハンドルはライブラリが描く

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleVar(2);
    ed::PopStyleColor(3);

    // ユーザーがハンドルで変えた大きさを読み返す。GetNodeSize は「ノード全体」、width/height は
    // 「枠」の寸法なので差分を引く。引かないと保存と読込のたびに枠が余白ぶん太っていく。
    const ImVec2 actualSize = ed::GetNodeSize(node.id);
    const ImVec2 extra      = GroupFrameExtra();
    if (actualSize.x > 10.0f && actualSize.y > 10.0f)
    {
        node.width  = (std::max)(100.0f, actualSize.x - extra.x);
        node.height = (std::max)(50.0f, actualSize.y - extra.y);
    }
}

bool BehaviorTreeWindow::DrawTreeNode(EditorBTNode &node, const BehaviorTreeComponent *activeAI)
{
    const bool hasChildren = HasChildren(node.id);
    const bool isCollapsed = node.collapsed && hasChildren;
    bool       toggled     = false;

    ImColor borderColor;
    float   borderWidth = 1.0f;
    GetTreeNodeBorder(node, activeAI, borderColor, borderWidth);

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(25, 25, 25, 240));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, borderWidth);

    ed::BeginNode(node.id);
    ImGui::PushID(static_cast<int>(node.id)); // 開閉ボタンのIDをノードごとに分ける

    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // 上辺：入力ピン（UE5 風の横長バー。高さ 20px は掴みやすさのため）
    if (node.inputPin.id != 0)
    {
        ed::BeginPin(node.inputPin.id, ed::PinKind::Input);
        ImGui::Dummy(ImVec2(kNodeWidth, 20.0f));
        const ImVec2 pMin = ImGui::GetItemRectMin();
        const ImVec2 pMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 14), ImVec2(pMax.x - 2, pMax.y - 2), ImColor(180, 180, 180), 2.0f);
        ed::EndPin();
    }

    // 見出し：種類の色の帯に名前
    ImGui::BeginGroup();
    const ImVec2 headerStart = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(kNodeWidth, 32.0f));
    const ImVec2 headerEnd = ImGui::GetItemRectMax();
    drawList->AddRectFilled(headerStart, headerEnd, HeaderColorOf(node.type), 4.0f, ImDrawFlags_RoundCornersTop);

    ImGui::SetCursorScreenPos(ImVec2(headerStart.x + 10, headerStart.y + 8));
    ImGui::Text("%s", node.name.c_str());

    // 開閉ボタン（見出しの右端）。矢印はフォントの字形でなく図形で描かれるので、日本語フォントでも欠けない。
    if (CanCollapse(node) && hasChildren)
    {
        ImGui::SetCursorScreenPos(ImVec2(headerEnd.x - 26.0f, headerStart.y + 5.0f));
        if (ImGui::ArrowButton("##collapse", isCollapsed ? ImGuiDir_Right : ImGuiDir_Down))
            toggled = true;
        if (ImGui::IsItemHovered())
        {
            // キャンバスの拡縮を受けない画面座標でツールチップを出すため、一時的に抜ける
            ed::Suspend();
            ImGui::SetTooltip(isCollapsed ? "子孫を表示する（ノードのダブルクリックでも可）"
                                          : "子孫を畳む（ノードのダブルクリックでも可）");
            ed::Resume();
        }
    }
    ImGui::EndGroup();

    DrawTreeNodeDetail(node, activeAI, isCollapsed);

    // 下辺：出力ピン。畳んでいる間はピンを出さず「重なった札」を描く。
    //   ピンを出すと、隠れた子の並びの中へ見えないまま新しい子を繋げてしまう。
    if (isCollapsed)
    {
        ImGui::Spacing();
        ImGui::Dummy(ImVec2(kNodeWidth, 20.0f));
        const ImVec2 pMin = ImGui::GetItemRectMin();
        const ImVec2 pMax = ImGui::GetItemRectMax();
        for (int i = 2; i >= 0; --i) // 奥の札から描く
        {
            const float o     = 4.0f * static_cast<float>(i);
            const int   shade = 25 * (2 - i);
            drawList->AddRectFilled(ImVec2(pMin.x + 2 + o, pMin.y + 2 + o * 0.5f),
                                    ImVec2(pMax.x - 2 - o, pMax.y - 10 + o * 0.5f),
                                    ImColor(70 + shade, 110 + shade, 140 + shade), 2.0f);
        }
    }
    else if (node.outputPin.id != 0)
    {
        ImGui::Spacing();
        ed::BeginPin(node.outputPin.id, ed::PinKind::Output);
        ImGui::Dummy(ImVec2(kNodeWidth, 20.0f));
        const ImVec2 pMin = ImGui::GetItemRectMin();
        const ImVec2 pMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 2), ImVec2(pMax.x - 2, pMax.y - 6), ImColor(100, 100, 100), 2.0f);
        ed::EndPin();
    }

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleVar(2);
    ed::PopStyleColor(2);

    return toggled;
}

void BehaviorTreeWindow::DrawTreeNodeDetail(const EditorBTNode &node, const BehaviorTreeComponent *activeAI, bool isCollapsed)
{
    const ImVec4 kDetailColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

    ImGui::BeginGroup();
    ImGui::Indent(10.0f);
    ImGui::Spacing();

    if (node.executionOrder != -1)
        ImGui::TextColored(ImVec4(1, 1, 0, 0.8f), "Priority: %d", node.executionOrder);

    if (isCollapsed)
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "+%d ノードを畳み中", CountDescendants(node.id));

    const std::optional<float> timer = DebugTimerOf(node, activeAI);

    if (node.type == EditorBTNodeType::Action)
    {
        // データの技は「どの技か」が param にあるので、行動名ではなく技の名前を出す
        if (node.actionOrConditionId == A_DroneFormation)
            ImGui::TextColored(kDetailColor, "> %s", GetDroneFormationLabel(node.param));
        else if (const char *paramLabel = GetActionParamLabel(node.actionOrConditionId))
            ImGui::TextColored(kDetailColor, "> %s  [%s %.2f]", GetActionDisplayName(node.actionOrConditionId), paramLabel, node.param);
        else
            ImGui::TextColored(kDetailColor, "> %s", GetActionDisplayName(node.actionOrConditionId));

        if (timer && *timer > 0.0f && IsWaitAction(node.actionOrConditionId))
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Wait: %.1fs", *timer);
    }
    else if (node.type == EditorBTNodeType::Condition)
    {
        // 間合いなどの param も出す。出さないと「距離が N m 未満」が並ぶだけで、どれが何mか分からない
        if (const char *paramLabel = GetConditionParamLabel(node.actionOrConditionId))
            ImGui::TextColored(kDetailColor, "? %s  [%s %.1f]", GetConditionDisplayName(node.actionOrConditionId), paramLabel, node.param);
        else
            ImGui::TextColored(kDetailColor, "? %s", GetConditionDisplayName(node.actionOrConditionId));
    }
    else if (node.type == EditorBTNodeType::Decorator)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "* %s", DecoratorNameOf(node.decoratorType));

        // Cooldown の残り時間（ゲーム実行中のみ）。ゲージは溜まるほど右へ伸びる
        const bool isCooldown = (node.decoratorType == 1);
        if (isCooldown && timer && *timer > 0.0f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cooling: %.1fs", *timer);
            const float fraction = 1.0f - *timer / (std::max)(0.001f, node.param);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::ProgressBar(fraction, ImVec2(kNodeWidth - 20, 6), "");
            ImGui::PopStyleColor();
        }
        else if (isCooldown && timer)
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Ready");
        }
    }

    ImGui::Unindent(10.0f);
    ImGui::Spacing();
    ImGui::EndGroup();
}

void BehaviorTreeWindow::GetTreeNodeBorder(const EditorBTNode &node, const BehaviorTreeComponent *activeAI,
                                           ImColor &outColor, float &outWidth) const
{
    switch (DebugStateOf(node, activeAI))
    {
    case BTDebugState::Running:
    {
        const float blink = std::sin(static_cast<float>(ImGui::GetTime()) * 15.0f) * 0.5f + 0.5f; // 黄色く脈打つ
        outColor          = ImColor(255, 200, 0, 200 + static_cast<int>(55 * blink));
        outWidth          = 3.0f;
        return;
    }
    case BTDebugState::Success:
        outColor = ImColor(50, 255, 50, 200);
        outWidth = 2.0f;
        return;
    case BTDebugState::Failure:
        outColor = ImColor(255, 50, 50, 200);
        outWidth = 2.0f;
        return;
    default:
        break;
    }

    outColor = ImColor(60, 60, 60, 255);
    outWidth = 1.0f;
}

// ====================================================================
// リンク
// ====================================================================
void BehaviorTreeWindow::DrawLinks(const BehaviorTreeComponent *activeAI)
{
    for (const auto &link : m_links)
    {
        auto parent = m_pinOwner.find(link.startPinId);
        auto child  = m_pinOwner.find(link.endPinId);

        // 隠れたノードへの線は描かない（畳んだノードは出力ピンを出していないので、繋ぎ先が無い）
        if ((parent != m_pinOwner.end() && m_hiddenNodes.count(parent->second)) ||
            (child != m_pinOwner.end() && m_hiddenNodes.count(child->second)))
            continue;

        // 親ノードの実行状態で色を変える：実行中=橙の点滅 / 成功=緑 / 失敗=赤
        ImVec4 color     = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        float  thickness = 2.0f;

        const EditorBTNode *parentNode = (parent != m_pinOwner.end()) ? FindNode(parent->second) : nullptr;
        switch (parentNode ? DebugStateOf(*parentNode, activeAI) : BTDebugState::None)
        {
        case BTDebugState::Running:
        {
            const float pulse = (std::sin(static_cast<float>(ImGui::GetTime()) * 10.0f) + 1.0f) * 0.5f;
            color             = ImVec4(1.0f, 0.6f, 0.0f, 0.5f + pulse * 0.5f);
            thickness         = 4.0f;
            break;
        }
        case BTDebugState::Success:
            color     = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
            thickness = 3.0f;
            break;
        case BTDebugState::Failure:
            color     = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
            thickness = 3.0f;
            break;
        default:
            break;
        }

        ed::Link(link.id, link.startPinId, link.endPinId, color, thickness);
    }
}

void BehaviorTreeWindow::FollowRunningNode(const BehaviorTreeComponent *activeAI)
{
    // 実行中の見えているノードのうち、末端の Action を優先する。
    //   親の Sequence なども Running になるが、実際に作業しているのは Action なので。
    uint32_t target      = 0;
    bool     foundAction = false;
    for (const auto &node : m_nodes)
    {
        if (node.type == EditorBTNodeType::Group || m_hiddenNodes.count(node.id)) continue;
        if (DebugStateOf(node, activeAI) != BTDebugState::Running) continue;
        if (foundAction && node.type != EditorBTNodeType::Action) continue;

        target      = node.id;
        foundAction = foundAction || node.type == EditorBTNodeType::Action;
    }

    if (target == 0)
    {
        m_lastTrackedNodeId = 0; // 実行が止まったら、次に動いたとき改めて寄せる
        return;
    }
    if (target == m_lastTrackedNodeId) return;

    // ライブラリは「選択へ寄せる」しか持たないので、一瞬だけ対象を選択して寄せ、選択を元に戻す
    // （インスペクタで見ているノードを奪わない）。倍率は変えない。
    std::vector<ed::NodeId> selected(static_cast<size_t>(ed::GetSelectedObjectCount()));
    selected.resize(static_cast<size_t>(ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()))));

    ed::ClearSelection();
    ed::SelectNode(target);
    ed::NavigateToSelection(false, 0.4f);

    ed::ClearSelection();
    for (auto id : selected)
        ed::SelectNode(id, true);

    m_lastTrackedNodeId = target;
}

// ====================================================================
// 入力（リンク作成・削除・メニュー）
// ====================================================================
void BehaviorTreeWindow::HandleLinkCreation()
{
    if (ed::BeginCreate())
    {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId) && startPinId && endPinId)
        {
            auto isInputPin = [&](ed::PinId pin) {
                const uint32_t id = ToEditorId(pin.Get());
                return std::any_of(m_nodes.begin(), m_nodes.end(), [id](const EditorBTNode &n) { return n.inputPin.id == id; });
            };
            const bool startIsInput = isInputPin(startPinId);
            const bool endIsInput   = isInputPin(endPinId);

            if (startIsInput == endIsInput)
            {
                ed::RejectNewItem(ImColor(255, 0, 0), 2.0f); // 入力どうし・出力どうしは繋げない
            }
            else if (ed::AcceptNewItem())
            {
                // ドラッグの向きによらず「親の出力 → 子の入力」の向きで持つ
                const uint32_t outPin = ToEditorId((startIsInput ? endPinId : startPinId).Get());
                const uint32_t inPin  = ToEditorId((startIsInput ? startPinId : endPinId).Get());
                m_links.push_back({GetNextId(), outPin, inPin});
                RecordStateForUndo("Add Link");
            }
        }
    }
    ed::EndCreate();
}

void BehaviorTreeWindow::HandleDeletion()
{
    bool deleted = false;
    if (ed::BeginDelete())
    {
        ed::NodeId nodeId = 0;
        while (ed::QueryDeletedNode(&nodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                DeleteNodeAndLinks(ToEditorId(nodeId.Get()));
                deleted = true;
            }
        }

        ed::LinkId linkId = 0;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                const uint32_t id = ToEditorId(linkId.Get());
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(), [id](const BTLink &l) { return l.id == id; }),
                              m_links.end());
                deleted = true;
            }
        }
    }
    ed::EndDelete();

    if (deleted) RecordStateForUndo("Delete Node/Link");
}

void BehaviorTreeWindow::FitInitialView()
{
    if (!m_needInitialZoom) return;

    // ノードの幅と高さが確定するまで待つ（3フレーム目以降）
    if (++m_framesSinceOpen <= 2) return;

    // ノードが無いと NavigateToContent がゼロ除算で壊れる。
    // 時間 0（即時）にするのは、補間中に NaN が増殖するのを防ぐため。
    if (!m_nodes.empty()) ed::NavigateToContent(0.0f);
    m_needInitialZoom = false;
}

void BehaviorTreeWindow::HandleContextMenu()
{
    ed::Suspend();

    // =======================================================
    //  Spaceキーでマウスカーソルの位置にメニューを出す
    // =======================================================
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePos(); // Spaceを押した瞬間のマウス座標を記憶
    }

    if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("Create New Node"))
    {
        ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);

        if (ImGui::MenuItem("セレクター (Selector)")) CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("シーケンス (Sequence)")) CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("並行ノード（Parallel）"))
        {
            CreateNode(EditorBTNodeType::Parallel, "並行ノード（Parallel）", canvasPos.x, canvasPos.y);
        }
        if (ImGui::MenuItem("重み付きセレクター（Weighted Selector）"))
        {
            CreateNode(EditorBTNodeType::WeightedSelector, "重み付きセレクター（Weighted Selector）", canvasPos.x, canvasPos.y);
        }
        ImGui::Separator();

        if (ImGui::MenuItem("デコレーター (Decorator)")) CreateNode(EditorBTNodeType::Decorator, "デコレーター (Decorator)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("コンディション (Condition)")) CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("アクション (Action)")) CreateNode(EditorBTNodeType::Action, "アクション (Action)", canvasPos.x, canvasPos.y);
        ImGui::Separator();
        // 選択ノードを囲むのが本命。空の枠を置いてから手で広げるのは最後の手段。
        if (ImGui::MenuItem("選択ノードをコメントで囲む", "Ctrl+G")) GroupSelectedNodes();
        if (ImGui::MenuItem("空のコメント枠を置く")) CreateNode(EditorBTNodeType::Group, "コメント", canvasPos.x, canvasPos.y);

        ImGui::Separator();
        // 複数ノードの複製
        if (ImGui::MenuItem("選択ノードを複製 (Duplicate)"))
        {
            DuplicateSelectedNodes();
        }

        ImGui::EndPopup();
    }

    ed::Resume();
}
