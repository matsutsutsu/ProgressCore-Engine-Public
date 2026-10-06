#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include <algorithm>
#include <cmath>

using namespace BTEditor;

// ---------------------------------------------------------------------------
// 作業領域
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::DrawNodeWorkspace(const BehaviorTreeComponent *activeAI)
{
    // 1フレームの流れ:
    //   整列の予約を実行 → ノードを描く（今の階層に無いものは飛ばす）→ リンクを描く
    //   → 自動追従・サブツリーへ入る → リンク作成・削除・メニュー の順。
    //   整列は ed::Begin の中でしかノードのサイズが取れないので、ここで処理する。

    ZoneScopedN("BTW: NodeWorkspace");

    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50.0f || avail.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    // キャンバスの画面上の範囲。貼り付け先（カーソルが外なら画面の中央）の判定に使う
    m_canvasScreenMin  = ImGui::GetCursorScreenPos();
    m_canvasScreenSize = avail;

    ed::SetCurrentEditor(m_nodeContext);
    {
        ZoneScopedN("BTW: ed::Begin");
        ed::Begin("My Node Editor");
    }

    // 整列はここで実行する。ed::GetNodeSize が前フレームの実サイズを返すので、
    // 種類ごとの高さの違いを織り込んだ段組みができる。ed::Begin の外だと値が取れない。
    if (m_layoutRequested)
    {
        ZoneScopedN("BTW: AutoLayout");
        AutoLayout();
        m_layoutRequested = false;
    }

    // ノードの描画。ZoneValue は実際に描いた数（隠れているノードは数えない）。
    {
        ZoneScopedN("BTW: DrawNodes");
        int drawnNodes = 0;
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

            // 今の階層に無いノードは描かない。描かなかったノードとピンはライブラリ側で
            // 「生きていない」扱いになり、選択・接続・当たり判定から外れる。座標は上で預けてあるので
            // 保存時の GetNodePosition も正しく返る。
            if (m_hiddenNodes.count(node.id)) continue;

            ++drawnNodes;
            if (node.type == EditorBTNodeType::Group)
                DrawGroupNode(node);
            else
                DrawTreeNode(node, activeAI);

            // ユーザーのドラッグを手元の座標へ写す（実行順の判定はこの値を使う）
            const ImVec2 p = ed::GetNodePosition(node.id);
            node.posX      = p.x;
            node.posY      = p.y;
        }
        ZoneValue(drawnNodes);
    }

    // 貼り付けたノードを選択する。上のループで一度描いた後でないと、ライブラリが選択を受け付けない
    if (!m_pendingSelectIds.empty())
    {
        ed::ClearSelection();
        for (uint32_t id : m_pendingSelectIds)
            ed::SelectNode(id, true);
        m_pendingSelectIds.clear();
    }

    DrawLinks(activeAI);

    if (m_autoFollow)
    {
        ZoneScopedN("BTW: FollowRunningNode");
        FollowRunningNode(activeAI);
    }
    else
        m_lastTrackedNodeId = 0;

    // SubTree のダブルクリックは、その中へ入る（切り替えは次フレームの頭）
    {
        const uint32_t      dbl = ToEditorId(ed::GetDoubleClickedNode().Get());
        const EditorBTNode *n   = (dbl != 0) ? FindNode(dbl) : nullptr;
        if (n && n->type == EditorBTNodeType::SubTree) RequestEnterSubTree(dbl);
    }

    {
        ZoneScopedN("BTW: HandleLinkCreation");
        HandleLinkCreation();
    }

    // 文字入力中（インスペクタの名前欄など）は効かせない。Ctrl+C が文字のコピーとノードのコピーの両方に効いてしまう
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_D)) DuplicateSelectedNodes();
        if (ImGui::IsKeyPressed(ImGuiKey_G)) GroupSelectedNodes();
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) CopySelection();
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) CutSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) PasteClipboard(PasteTargetOnCanvas());
    }

    {
        ZoneScopedN("BTW: HandleDeletion");
        HandleDeletion();
    }
    {
        ZoneScopedN("BTW: HandleContextMenu");
        HandleContextMenu();
    }
    FitInitialView();

    // ライブラリ本体。ノードとリンクの実際の描画・当たり判定・選択はここでまとめて行われる。
    {
        ZoneScopedN("BTW: ed::End");
        ed::End();
    }
}

// ---------------------------------------------------------------------------
// ノードの描画
// ---------------------------------------------------------------------------
void BehaviorTreeWindow::DrawGroupNode(EditorBTNode &node)
{
    ZoneScopedN("BTW: DrawGroupNode");
    ZoneValue(node.id);

    // 選択中はオレンジで光らせ、それ以外は役割ごとの色。塗り分けておくと、
    // ズームアウトしたままでも「どの塊の話か」が読める。
    const GroupPalette &palette     = GroupPaletteAt(node.groupColorIndex);
    const bool          isSelected  = ed::IsNodeSelected(node.id);
    const ImColor       borderColor = isSelected ? ImColor(255, 165, 0, 255) : ImColor(palette.border);
    const ImColor       headerColor = isSelected ? ImColor(150, 95, 20, 245) : ImColor(palette.header);

    // ノード背景は透明にする。コメント枠もノードなので、まず全面に StyleColor_NodeBg が敷かれ、
    // その上に薄い枠色を重ねてもほとんど色が出ない。
    // 色は BeginNode の時点で確定するので、Push は必ず BeginNode より前に置く。
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

ImVec2 BehaviorTreeWindow::PasteTargetOnCanvas()
{
    // キャンバスの中では、ライブラリがマウス座標をキャンバス座標へ書き換えている
    //   （imgui_canvas.cpp）。画面座標で判定するため、一時的にキャンバスを抜ける。
    //   抜けずに ScreenToCanvas を掛けると二重に変換され、左上の遠くへ貼られる。
    ed::Suspend();
    const ImVec2 mouse  = ImGui::GetMousePos();
    const ImVec2 min    = m_canvasScreenMin;
    const ImVec2 max    = ImVec2(min.x + m_canvasScreenSize.x, min.y + m_canvasScreenSize.y);
    const bool   inside = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;

    // カーソルがキャンバスの外（アウトラインの上など）なら、今見ている範囲の中央へ
    const ImVec2 target = inside ? mouse : ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const ImVec2 canvas = ed::ScreenToCanvas(target);
    ed::Resume();
    return canvas;
}

void BehaviorTreeWindow::DrawTreeNode(EditorBTNode &node, const BehaviorTreeComponent *activeAI)
{
    // ノード1つ分。ZoneValue はノードID（重いノードをエディタ上で探すため）。
    ZoneScopedN("BTW: DrawTreeNode");
    ZoneValue(node.id);

    ImColor borderColor;
    float   borderWidth = 1.0f;
    GetTreeNodeBorder(node, activeAI, borderColor, borderWidth);

    ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(25, 25, 25, 240));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, borderWidth);

    ed::BeginNode(node.id);
    ImGui::PushID(static_cast<int>(node.id)); // ノード内の部品のIDをノードごとに分ける

    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // ノードの幅は名前の長さで決まる。ピン・見出し・ゲージをすべてこの幅で描く
    const std::string title = GetDisplayName(node);
    const float       nodeW = TreeNodeWidthOf(node, title);

    // 上辺：入力ピン（UE5 風の横長バー。高さ 20px は掴みやすさのため）
    if (node.inputPin.id != 0)
    {
        ed::BeginPin(node.inputPin.id, ed::PinKind::Input);
        ImGui::Dummy(ImVec2(nodeW, 20.0f));
        const ImVec2 pMin = ImGui::GetItemRectMin();
        const ImVec2 pMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 14), ImVec2(pMax.x - 2, pMax.y - 2), ImColor(180, 180, 180), 2.0f);
        ed::EndPin();
    }

    // 見出し：種類の色の帯に名前
    //   上限幅を超える名前は折り返し、帯の高さを行数に合わせて伸ばす。
    //   先に折り返した文字の高さを測り（CalcTextSize）、その高さで帯を塗ってから文字を書く。
    const float       titleWrapW = nodeW - 20.0f; // 左右の余白を除いた幅
    const float       titleH     = ImGui::CalcTextSize(title.c_str(), nullptr, false, titleWrapW).y;
    const float       headerH    = (std::max)(32.0f, titleH + 16.0f); // 1行なら 32px

    ImGui::BeginGroup();
    const ImVec2 headerStart = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(nodeW, headerH));
    const ImVec2 headerEnd = ImGui::GetItemRectMax();
    drawList->AddRectFilled(headerStart, headerEnd, HeaderColorOf(node.type), 4.0f, ImDrawFlags_RoundCornersTop);

    // 折り返し幅を直接渡して描く。PushTextWrapPos はウィンドウ左端からの位置で指定するので、
    // キャンバスを原点より左へ動かすと位置が負になり、ImGui が「折り返さない」とみなす。
    // そうなると文字がノードの外へはみ出し、見た目と掴める範囲（ノードの枠）がずれる。
    // 高さを測った CalcTextSize と同じ幅を渡すので、帯の高さと行数も必ず一致する。
    drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(headerStart.x + 10, headerStart.y + 8),
                      ImGui::GetColorU32(ImGuiCol_Text), title.c_str(), nullptr, titleWrapW);
    ImGui::EndGroup();

    {
        ZoneScopedN("BTW: DrawTreeNodeDetail");
        DrawTreeNodeDetail(node, activeAI, nodeW);
    }

    // 下辺：出力ピン
    if (node.outputPin.id != 0)
    {
        ImGui::Spacing();
        ed::BeginPin(node.outputPin.id, ed::PinKind::Output);
        ImGui::Dummy(ImVec2(nodeW, 20.0f));
        const ImVec2 pMin = ImGui::GetItemRectMin();
        const ImVec2 pMax = ImGui::GetItemRectMax();
        drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 2), ImVec2(pMax.x - 2, pMax.y - 6), ImColor(100, 100, 100), 2.0f);
        ed::EndPin();
    }

    ImGui::PopID();
    ed::EndNode();
    ed::PopStyleVar(2);
    ed::PopStyleColor(2);
}

float BehaviorTreeWindow::TreeNodeWidthOf(const EditorBTNode &node, const std::string &title) const
{
    float textW = ImGui::CalcTextSize(title.c_str()).x;

    // 本文の行も収める。実行中だけ出る行（残り時間）は、出る種類のノードなら最初から幅を取っておく。
    //   出たときに広げると、ゲーム中にノードの幅が伸び縮みして、並びや線がガタつく。
    auto fit = [&](const char *sample) { textW = (std::max)(textW, ImGui::CalcTextSize(sample).x); };
    if (node.executionOrder != -1) fit("Priority: 00");
    if (node.type == EditorBTNodeType::SubTree) fit("ダブルクリックで中へ");
    if (node.type == EditorBTNodeType::Action && IsWaitAction(node.actionOrConditionId)) fit("Wait: 00.0s");
    if (node.type == EditorBTNodeType::Decorator && node.decoratorType == 1) fit("Cooling: 00.0s");

    // 左右の余白 10px ずつ
    return (std::clamp)(textW + 20.0f, kNodeMinWidth, kNodeMaxWidth);
}

void BehaviorTreeWindow::DrawTreeNodeDetail(const EditorBTNode &node, const BehaviorTreeComponent *activeAI, float nodeWidth)
{
    // 行動名・条件名・デコレータ名は見出しに出ているので、ここには実行中の情報だけを出す
    ImGui::BeginGroup();
    ImGui::Indent(10.0f);
    ImGui::Spacing();

    if (node.executionOrder != -1)
        ImGui::TextColored(ImVec4(1, 1, 0, 0.8f), "Priority: %d", node.executionOrder);

    if (node.type == EditorBTNodeType::SubTree)
        ImGui::TextDisabled("ダブルクリックで中へ");

    const std::optional<float> timer = DebugTimerOf(node, activeAI);

    // Wait 系の残り時間（ゲーム実行中のみ）
    if (node.type == EditorBTNodeType::Action)
    {
        if (timer && *timer > 0.0f && IsWaitAction(node.actionOrConditionId))
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Wait: %.1fs", *timer);
    }
    else if (node.type == EditorBTNodeType::Decorator)
    {
        // Cooldown の残り時間（ゲーム実行中のみ）。ゲージは溜まるほど右へ伸びる
        const bool isCooldown = (node.decoratorType == 1);
        if (isCooldown && timer && *timer > 0.0f)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cooling: %.1fs", *timer);
            const float fraction = 1.0f - *timer / (std::max)(0.001f, node.param);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::ProgressBar(fraction, ImVec2(nodeWidth - 20.0f, 6), "");
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

// ---------------------------------------------------------------------------
// リンク
// ---------------------------------------------------------------------------
void BehaviorTreeWindow::DrawLinks(const BehaviorTreeComponent *activeAI)
{
    ZoneScopedN("BTW: DrawLinks");
    ZoneValue(m_links.size());

    for (const auto &link : m_links)
    {
        auto parent = m_pinOwner.find(link.startPinId);
        auto child  = m_pinOwner.find(link.endPinId);

        // 別の階層のノードへの線は描かない（そのノードは描かれていないので、繋ぎ先が無い）
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

// ---------------------------------------------------------------------------
// 入力（リンク作成・削除・メニュー）
// ---------------------------------------------------------------------------
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

        // 線を何も無い所で離した。繋ぎ元を覚えて、離した場所にノード追加メニューを出す（UE と同じ操作）。
        //   メニューは Suspend した後でないと画面座標で開けないので、HandleContextMenu へ予約する。
        ed::PinId pinId = 0;
        if (ed::QueryNewNode(&pinId) && ed::AcceptNewItem())
        {
            m_newNodeLinkPin        = ToEditorId(pinId.Get());
            m_openCreateMenuForLink = true;
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
            // サブツリーの入口は消させない。消すと、SubTree が中身を持っていても何も実行されなくなる
            const EditorBTNode *n = FindNode(ToEditorId(nodeId.Get()));
            if (n && n->type == EditorBTNodeType::Root && n->graphId != 0)
            {
                ed::RejectDeletedItem();
                continue;
            }
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

namespace
{
    // 子を繋げる出力ピンを持つ種類か（CreateNode のピンの発行と同じ規則）
    bool HasOutputPin(EditorBTNodeType type)
    {
        return type != EditorBTNodeType::Condition && type != EditorBTNodeType::Action &&
               type != EditorBTNodeType::SubTree && type != EditorBTNodeType::Group;
    }
} // namespace

void BehaviorTreeWindow::LinkNewNodeToPin(uint32_t newNodeId, uint32_t fromPinId)
{
    const EditorBTNode *n = FindNode(newNodeId);
    if (!n) return;

    const bool fromInput = std::any_of(m_nodes.begin(), m_nodes.end(),
                                       [fromPinId](const EditorBTNode &o) { return o.inputPin.id == fromPinId; });
    if (!fromInput)
    {
        // 親の出力から伸ばした → 新しいノードはその子
        if (n->inputPin.id != 0) m_links.push_back({GetNextId(), fromPinId, n->inputPin.id});
        return;
    }

    // 子の入力から伸ばした → 新しいノードはその親。子の親は1つなので、元の親からの線は外す
    if (n->outputPin.id == 0) return;
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [fromPinId](const BTLink &l) { return l.endPinId == fromPinId; }),
                  m_links.end());
    m_links.push_back({GetNextId(), n->outputPin.id, fromPinId});
}

void BehaviorTreeWindow::HandleContextMenu()
{
    ed::Suspend();

    // =======================================================
    //  Spaceキーでマウスカーソルの位置にメニューを出す
    // =======================================================
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePos(); // Spaceを押した瞬間のマウス座標を記憶
        m_newNodeLinkPin  = 0;
    }

    if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
        m_newNodeLinkPin  = 0;
    }

    // ピンから伸ばした線を何も無い所で離した（HandleLinkCreation が予約）。離した場所にノードを作る
    if (m_openCreateMenuForLink)
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos       = ImGui::GetMousePos();
        m_openCreateMenuForLink = false;
    }

    if (ImGui::BeginPopup("Create New Node"))
    {
        ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);

        // 線から開いたときは、繋げられる種類だけを選べるようにする。
        //   入力ピン（子の上辺）から伸ばしたなら新しいノードは親なので、子を持てない種類は選べない。
        const bool fromLink  = (m_newNodeLinkPin != 0);
        const bool fromInput = fromLink && std::any_of(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode &n) {
                                   return n.inputPin.id == m_newNodeLinkPin;
                               });

        uint32_t created = 0;
        auto     item    = [&](const char *label, EditorBTNodeType type) {
            const bool enabled = !fromInput || HasOutputPin(type);
            if (!ImGui::MenuItem(label, nullptr, false, enabled)) return;
            created = (type == EditorBTNodeType::SubTree) ? CreateSubTreeNode(canvasPos.x, canvasPos.y)
                                                          : CreateNode(type, label, canvasPos.x, canvasPos.y);
        };

        item("セレクター (Selector)", EditorBTNodeType::Selector);
        item("シーケンス (Sequence)", EditorBTNodeType::Sequence);
        item("並行ノード（Parallel）", EditorBTNodeType::Parallel);
        item("重み付きセレクター（Weighted Selector）", EditorBTNodeType::WeightedSelector);
        ImGui::Separator();

        item("デコレーター (Decorator)", EditorBTNodeType::Decorator);
        item("コンディション (Condition)", EditorBTNodeType::Condition);
        item("アクション (Action)", EditorBTNodeType::Action);
        item("サブツリー (SubTree)", EditorBTNodeType::SubTree);

        // 選択やクリップボードを扱う項目は、線から開いたときには出さない（繋ぐ相手が決まらない）
        if (!fromLink)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("選択をサブツリーにまとめる")) CollapseSelectionToSubTree();
            // 選択ノードを囲むのが本命。空の枠を置いてから手で広げるのは最後の手段。
            if (ImGui::MenuItem("選択ノードをコメントで囲む", "Ctrl+G")) GroupSelectedNodes();
            if (ImGui::MenuItem("空のコメント枠を置く")) CreateNode(EditorBTNodeType::Group, "コメント", canvasPos.x, canvasPos.y);

            ImGui::Separator();
            // 複数ノードの複製
            if (ImGui::MenuItem("選択ノードを複製 (Duplicate)", "Ctrl+D"))
            {
                DuplicateSelectedNodes();
            }
            if (ImGui::MenuItem("コピー", "Ctrl+C")) CopySelection();
            if (ImGui::MenuItem("切り取り", "Ctrl+X")) CutSelection();
            if (ImGui::MenuItem("ここに貼り付け", "Ctrl+V")) PasteClipboard(canvasPos);
        }

        if (created != 0 && fromLink)
        {
            LinkNewNodeToPin(created, m_newNodeLinkPin);
            m_newNodeLinkPin = 0;
            RecordStateForUndo("Create Linked Node");
        }

        ImGui::EndPopup();
    }
    else
    {
        m_newNodeLinkPin = 0; // 何も選ばずにメニューを閉じた
    }

    ed::Resume();
}
