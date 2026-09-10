#include "AnimGraphWindow.h"
#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Core/Math/StringHash.h"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

#include "Engine/Animation/Components/AnimParametersComponent.h"

#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <filesystem>
#include "Engine/ECS/Core/CCL_World.h"
#include <cmath> // sinf用
#include <unordered_map>
#include <sstream>
#include <iomanip>

namespace ed = ax::NodeEditor;


// =====================================================================
// 安定ID採番ヘルパー
//   ノード = hash*4+0 / 入力ピン = +1 / 出力ピン = +2 （スロット分離で隣接衝突を排除）
//   予約ID(Entry/AnyState)は「4で割って余り3」の負値に固定する。
//   ノード(≡0)・入力(≡1)・出力(≡2) のどれとも mod4 が一致しないため、
//   どんなハッシュ値が来ても予約IDと絶対に衝突しない（スロット3は未使用）。
// =====================================================================
namespace {
    inline int StateNodeId(uint32_t h) { return (int)(h * 4u + 0u); }

    // 遷移(リンク)1本ごとの端点ピン。種は linkId（遷移ごとに一意）。
    //   下位2ビットが「何のIDか」を表す規則を全体で共有している:
    //     0 = ステートノード / 1 = 遷移の出力ピン / 2 = 遷移の入力ピン / 3 = 予約ID
    //   この規則に乗せると、ハッシュを使わずに衝突しないことが採番だけで保証できる。
    inline int TransOutPin(int linkId) { return linkId * 4 + 1; }
    inline int TransInPin (int linkId) { return linkId * 4 + 2; }

    // 予約ID（すべて mod4 == 3。ノード/ピンのスロット0,1,2と交差しない）
    constexpr int ENTRY_NODE_ID     = -1;   // 0xFFFFFFFF & 3 == 3
    constexpr int ENTRY_LINK_ID     = -9;   // & 3 == 3
    constexpr int ANY_STATE_NODE_ID = -13;  // & 3 == 3

    // リンクID。素性解決は m_linkLut で行うため、ここは「同フレーム内で一意」であれば足りる。
    constexpr int NORMAL_LINK_BASE = 0x00100000;
    constexpr int ANY_LINK_BASE    = 0x00200000;

    // ツリー全体（root＋全サブグラフ）の「全 transition リスト」へ関数を適用する。
    //   パラメータ削除/リネーム時に、階層のどこにある条件も漏れなく張り替えるために使う。
    template <class F>
    void ForEachTransitionList(AnimStateGraph &g, F fn)
    {
        for (auto &s : g.states)
        {
            fn(s.transitions);
            if (s.isSubMachine && s.subGraph) ForEachTransitionList(*s.subGraph, fn);
        }
        fn(g.anyStateTransitions);
    }
}

// =====================================================================
// エディタ内での遷移エフェクト（パルス）を管理するためのローカル状態
// =====================================================================
static uint32_t s_prevActiveState = 0;
static uint32_t s_highlightSrc    = 0;
static uint32_t s_highlightDst    = 0;
static float    s_highlightTimer  = 0.0f;

// =====================================================================
// 矢印(三角形)のみを描画するヘルパー関数
// =====================================================================
static void DrawTriangleArrow(ImDrawList *drawList, const ImVec2 &p0, const ImVec2 &p1, ImU32 color)
{
    ImVec2 dir    = ImVec2(p1.x - p0.x, p1.y - p0.y);
    float  length = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (length < 0.001f)
        return;
    dir.x /= length;
    dir.y /= length;

    // 矢印を線の真ん中ではなく、ターゲットノードの「直前（80px手前）」に配置する (Unity風)
    float  offset   = (std::min)(length * 0.8f, 80.0f);
    ImVec2 arrowPos = ImVec2(p1.x - dir.x * offset, p1.y - dir.y * offset);

    float  arrowSize = 7.0f;
    ImVec2 perp      = ImVec2(-dir.y, dir.x);

    ImVec2 pt1 = ImVec2(arrowPos.x + dir.x * arrowSize, arrowPos.y + dir.y * arrowSize);
    ImVec2 pt2 = ImVec2(arrowPos.x - dir.x * arrowSize + perp.x * arrowSize * 0.8f, arrowPos.y - dir.y * arrowSize + perp.y * arrowSize * 0.8f);
    ImVec2 pt3 = ImVec2(arrowPos.x - dir.x * arrowSize - perp.x * arrowSize * 0.8f, arrowPos.y - dir.y * arrowSize - perp.y * arrowSize * 0.8f);

    drawList->AddTriangleFilled(pt1, pt2, pt3, color);
}

// =====================================================================
// アニメーショングラフを人間が読めるテキスト形式にダンプする
// =====================================================================
static std::string DumpAnimGraphToString(const AnimStateGraph& graph) {
    std::stringstream ss;

    // 1. Entry State (開始地点) の特定
    std::string entryName = "None";
    for (const auto& s : graph.states) {
        if (s.stateHash == graph.entryStateHash) {
            entryName = s.sequenceName;
            break;
        }
    }
    ss << "========================================================\n";
    ss << "[ ENTRY STATE ] -> " << entryName << "\n";
    ss << "========================================================\n\n";

    // 2. Any State Transitions (特別遷移)
    if (!graph.anyStateTransitions.empty()) {
        ss << "[ ANY STATE ] (どこからでも遷移可能)\n";
        for (const auto& t : graph.anyStateTransitions) {
            std::string targetName = "Unknown";
            for (const auto& s : graph.states) {
                if (s.stateHash == t.targetStateHash) targetName = s.sequenceName;
            }
            ss << "  => " << targetName
                << "  (Blend: " << std::fixed << std::setprecision(2) << t.blendDuration
                << "s, Logic: " << (t.logicType == AnimTransitionLogic::AND ? "AND" : "OR") << ")\n";

            for (const auto& c : t.conditions) {
                ss << "     - [" << c.paramName << "] ";
                switch (c.op) {
                case AnimConditionOp::Equal:    ss << "== " << c.threshold; break;
                case AnimConditionOp::Greater:  ss << "> " << c.threshold; break;
                case AnimConditionOp::Less:     ss << "< " << c.threshold; break;
                case AnimConditionOp::Trigger:  ss << "Triggered"; break;
                case AnimConditionOp::Finished: ss << "Finished"; break;
                }
                ss << "\n";
            }
        }
        ss << "\n";
    }

    // 3. 各ステートとそこから伸びる遷移リスト
    for (const auto& state : graph.states) {
        ss << "[ STATE ] " << state.sequenceName << "\n";
        ss << "  File:   " << (state.sequenceFilePath.empty() ? "None" : state.sequenceFilePath) << "\n";
        ss << "  Loop:   " << (state.isLoop ? "True" : "False") << "\n";
        ss << "  Speed:  " << std::fixed << std::setprecision(2) << state.playbackSpeed << "x\n";

        if (!state.speedCurveName.empty())      ss << "  Speed Curve: " << state.speedCurveName << "\n";
        if (!state.rootMotionCurveName.empty()) ss << "  RM Curve:    " << state.rootMotionCurveName << "\n";

        if (state.transitions.empty()) {
            ss << "  -> (No Outgoing Transitions)\n";
        }
        else {
            for (const auto& t : state.transitions) {
                std::string targetName = "Unknown";
                for (const auto& s : graph.states) {
                    if (s.stateHash == t.targetStateHash) targetName = s.sequenceName;
                }
                ss << "  => " << targetName
                    << "  (Blend: " << std::fixed << std::setprecision(2) << t.blendDuration
                    << "s, Logic: " << (t.logicType == AnimTransitionLogic::AND ? "AND" : "OR") << ")\n";

                for (const auto& c : t.conditions) {
                    ss << "     - [" << c.paramName << "] ";
                    switch (c.op) {
                    case AnimConditionOp::Equal:    ss << "== " << c.threshold; break;
                    case AnimConditionOp::Greater:  ss << "> " << c.threshold; break;
                    case AnimConditionOp::Less:     ss << "< " << c.threshold; break;
                    case AnimConditionOp::Trigger:  ss << "Triggered"; break;
                    case AnimConditionOp::Finished: ss << "Finished"; break;
                    }
                    ss << "\n";
                }
            }
        }
        ss << "--------------------------------------------------------\n";
    }

    return ss.str();
}

AnimGraphWindow::AnimGraphWindow() : EditorWindow("Anim Graph Editor") {
    m_nodeContext = ed::CreateEditor();
}

AnimGraphWindow::~AnimGraphWindow() {
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}

// =====================================================================
// 階層ナビゲーション
// =====================================================================

// m_editPath に沿って m_rootGraph を辿り、「今編集中の階層グラフ」を返す。
//   途中でサブマシンが見つからない（削除された等）場合は root へ戻す（安全策）。
AnimStateGraph &AnimGraphWindow::CurrentView()
{
    AnimStateGraph *g = &m_rootGraph;
    for (uint32_t h : m_editPath)
    {
        AnimState *sub = nullptr;
        for (auto &s : g->states)
            if (s.stateHash == h && s.isSubMachine && s.subGraph) { sub = &s; break; }
        if (!sub) { m_editPath.clear(); return m_rootGraph; }
        g = sub->subGraph.get();
    }
    return *g;
}

// 指定サブマシンが現在の階層に実在する時だけ潜る。
void AnimGraphWindow::EnterSubMachine(uint32_t subMachineHash)
{
    AnimStateGraph &view = CurrentView();
    for (auto &s : view.states)
        if (s.stateHash == subMachineHash && s.isSubMachine && s.subGraph)
        {
            m_editPath.push_back(subMachineHash);
            m_selectedNodeId = 0;
            m_selectedLinkId = 0;
            m_needRestorePositions = true; // 潜り先のノード位置を復元する
            return;
        }
}

// 階層全体で衝突しない stateHash を採る（同名衝突は評価器の誤爆やパス崩れの元）。
//   衝突時は name に "_2","_3"... を付与して一意化し、その最終 name の hash を返す。
uint32_t AnimGraphWindow::MakeUniqueStateHash(std::string &name)
{
    const std::string base = name;
    for (int i = 1; i < 10000; ++i)
    {
        uint32_t h = CCL::Utils::HashString(name.c_str());
        if (!FindStateRecursive(m_rootGraph, h)) return h;
        name = base + "_" + std::to_string(i + 1); // 次の候補名
    }
    return CCL::Utils::HashString(name.c_str());
}

// 「Root > Combat > ...」のパンくず。各段クリックでその階層へ戻る。
void AnimGraphWindow::DrawBreadcrumb()
{
    if (ImGui::Button("Root")) { m_editPath.clear(); m_needRestorePositions = true; }

    AnimStateGraph *g = &m_rootGraph;
    for (size_t i = 0; i < m_editPath.size(); ++i)
    {
        const char *name = "?";
        AnimState  *sub  = nullptr;
        for (auto &s : g->states)
            if (s.stateHash == m_editPath[i]) { name = s.sequenceName.c_str(); sub = &s; break; }

        ImGui::SameLine(); ImGui::TextDisabled(">"); ImGui::SameLine();
        ImGui::PushID((int)i);
        const bool clicked = ImGui::Button(name);
        ImGui::PopID();
        if (clicked)
        {
            m_editPath.resize(i + 1); // この段まで戻る
            m_needRestorePositions = true;
            break;
        }
        if (sub && sub->subGraph) g = sub->subGraph.get(); else break;
    }

    // 潜っている階層に Entry が無いと、そのサブマシンに入っても何も再生されない。警告する。
    AnimStateGraph &view = CurrentView();
    if (!m_editPath.empty() && view.entryStateHash == 0 && !view.states.empty())
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
            "  [!] Entry 未設定（ノード右クリック → Set as Entry）");
    }
}

void AnimGraphWindow::DrawContents(EditorContext& context) {
    ed::SetCurrentEditor(m_nodeContext);

    // このフレームの頭で「潜る」予約を消化（前フレームのダブルクリック等）。
    //   描画中に m_editPath を書き換えると参照が飛ぶため、フレーム境界で処理する。
    if (m_pendingEnterSub != 0)
    {
        EnterSubMachine(m_pendingEnterSub);
        m_pendingEnterSub = 0;
    }

    // ================================================================
    // リアルタイム・デバッグ (選択中エンティティのステート同期)
    // ================================================================
    m_runtimeActiveStateHash = 0;
    // エンティティが選択されており、ワールドが存在する場合
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world) {
        auto* fsm = context.world->GetComponent<AnimStateMachineComponent>(context.selectedEntity);
        if (fsm) {
            // 現在のステートハッシュを記憶する
            m_runtimeActiveStateHash = fsm->currentStateHash;
        }
    }

    DrawToolbar();
    DrawBreadcrumb(); // 階層パンくず（Root > Combat > ...）

    // カラム幅の強制上書きを初回のみにし、ユーザーが境界線をドラッグできるようにする
    static bool initColumns = true;
    ImGui::Columns(2, "AnimGraphColumns");
    if (initColumns) {
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.75f);
        initColumns = false;
    }

    // ノードエディタの「仮想座標系」が漏れ出さないよう、ChildWindowで完全にカプセル化する
    ImGui::BeginChild("NodeWorkspaceChild", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawNodeWorkspace();
    ImGui::EndChild();

    ImGui::NextColumn();

    // 右カラムを上下分割：上=パラメータパネル / 下=インスペクタ（Unity Animator 風）
    // 上部はパラメータ数に応じて高さを取りつつ、下部インスペクタを必ず残す。
    // パラメータ一覧が潰れて見えないという要望を受け、上部の取り分を広げる（0.4→0.55）。
    const float paramPanelHeight = ImGui::GetContentRegionAvail().y * 0.55f;

    ImGui::BeginChild("ParameterPanelChild", ImVec2(0, paramPanelHeight), true);
    DrawParameterPanel(context);
    ImGui::EndChild();

    ImGui::BeginChild("InspectorChild", ImVec2(0, 0), false);
    DrawInspector();
    ImGui::EndChild();

    ImGui::Columns(1);
}


void AnimGraphWindow::DrawNodeWorkspace()
{
    // 「今見ている階層」を局所参照で束ねる（以降の m_currentGraph は現在の階層グラフを指す）。
    AnimStateGraph &m_currentGraph = CurrentView();

    ed::Begin("My Anim Node Editor");

    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f); // 直線
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 12.0f);
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));

    constexpr float NODE_WIDTH           = 150.0f;
    constexpr float NODE_HEIGHT_ESTIMATE = 42.0f; // ノードの概算高さ

    std::unordered_map<int, ImVec2> pinPositions;
    ImDrawList                     *drawList = ImGui::GetWindowDrawList();

    // --- ① ピンIDは遷移ごとに一意な linkId から採番する ---
    //   元は (src, dst) の組をハッシュしていたため、同じ元→先に遷移を2本張ると
    //   両者が同一のピンIDになった。node-editor は同じフレームで同じIDの BeginPin を
    //   2回受け取ると内部の対応表が壊れ、グラフを開いた瞬間にフリーズする。
    //   ハッシュをやめて TransOutPin/TransInPin の採番規則に乗せたので、
    //   ステートノードとも、出力↔入力どうしでも、衝突しないことが式で保証される。
    auto GetOutPinId = [](int linkId) -> int { return TransOutPin(linkId); };
    auto GetInPinId  = [](int linkId) -> int { return TransInPin(linkId); };

    // --- ② 全遷移の事前リスト化と、予測中心座標の計算 ---
    struct TransInfo
    {
        uint32_t src;
        uint32_t dst;
        bool     isAny;
        int      linkId;
    };
    std::vector<TransInfo> allTrans;

    int      linkIdCounter = NORMAL_LINK_BASE;
    uint32_t ENTRY_HASH    = 0;
    uint32_t ANY_HASH      = 0xFFFFFFFF;

    if (m_currentGraph.entryStateHash != 0)
        allTrans.push_back({ENTRY_HASH, m_currentGraph.entryStateHash, false, ENTRY_LINK_ID});

    for (const auto &trans : m_currentGraph.anyStateTransitions)
        allTrans.push_back({ANY_HASH, trans.targetStateHash, true, ANY_LINK_BASE + (linkIdCounter++)});

    for (const auto &s : m_currentGraph.states)
    {
        for (const auto &trans : s.transitions)
        {
            allTrans.push_back({s.stateHash, trans.targetStateHash, false, linkIdCounter++});
        }
    }

    // ノード中心をライブ座標で取得する。
    //   旧実装は保存済みの nodePosX/Y を使っていたため、ノードをドラッグした直後は
    //   「保存前の古い位置」から線が伸びてしまっていた（見た目バグ）。
    //   ed::GetNodePosition / GetNodeSize で現在の実座標・実サイズを使い解消する。
    //   返り値は「キャンバス座標」。スクリーン変換は呼び出し側で行う。
    auto GetNodeCenter = [&](uint32_t hash) -> ImVec2 {
        int nodeId;
        if (hash == ENTRY_HASH)
            nodeId = ENTRY_NODE_ID;
        else if (hash == ANY_HASH)
            nodeId = ANY_STATE_NODE_ID;
        else
            nodeId = StateNodeId(hash);

        ImVec2 pos  = ed::GetNodePosition(nodeId); // キャンバス座標（ライブ）
        ImVec2 size = ed::GetNodeSize(nodeId);     // 実サイズ（ライブ）

        // サイズが未確定（描画前）なら推定値でフォールバック
        if (size.x <= 0.0f)
            size.x = NODE_WIDTH;
        if (size.y <= 0.0f)
            size.y = NODE_HEIGHT_ESTIMATE;

        return ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
    };


    // 法線（直角）オフセットを計算 (進行方向に対して左へ8pxずらす)
    auto CalcNormalOffset = [](ImVec2 p0, ImVec2 p1) -> ImVec2 {
        ImVec2 dir = ImVec2(p1.x - p0.x, p1.y - p0.y);
        float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.001f)
            return ImVec2(0, 0);
        dir.x /= len;
        dir.y /= len;
        return ImVec2(-dir.y * 8.0f, dir.x * 8.0f);
    };

    // --- ③ ノード描画と「動的ピン配置」 ---
    auto DrawBlockNode = [&](int nodeId, uint32_t nodeHash, const char *title, ImVec4 bgColor, bool isActive) {
        // 強調の優先度: 実行中(青パルス) > エディタ選択(黄) > 通常
        const bool isSelected = (m_selectedNodeId == nodeId);

        ed::PushStyleColor(ed::StyleColor_NodeBg, bgColor);

        int pushedColors = 1; // NodeBg は常に push
        int pushedVars   = 0;

        if (isActive)
        {
            float pulse = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.4f + pulse * 0.4f, 0.8f + pulse * 0.2f, 1.0f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f + pulse * 1.5f);
            ++pushedColors;
            ++pushedVars;
        }
        else if (isSelected)
        {
            // エディタ選択中: 黄色の太枠（実行中でないときのみ）
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 3.0f);
            ++pushedColors;
            ++pushedVars;
        }

        ed::BeginNode(nodeId);

        ImVec2 startPos  = ImGui::GetCursorPos();
        ImVec2 screenPos = ImGui::GetCursorScreenPos();

        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(NODE_WIDTH, 14));
        std::string displayText = isActive ? (std::string("▶ ") + title) : title;
        float       textWidth   = ImGui::CalcTextSize(displayText.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, (NODE_WIDTH - textWidth) * 0.5f));
        ImGui::TextColored(isActive ? ImVec4(0.8f, 1.0f, 0.8f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", displayText.c_str());
        ImGui::Dummy(ImVec2(NODE_WIDTH, 14));
        ImGui::EndGroup();

        float  nodeHeight = ImGui::GetCursorPos().y - startPos.y;
        ImVec2 myCenter   = GetNodeCenter(nodeHash);

        for (const auto &t : allTrans)
        {
            if (t.src == nodeHash)
            {
                ImVec2 targetCenter = GetNodeCenter(t.dst);
                ImVec2 offset       = CalcNormalOffset(myCenter, targetCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetOutPinId(t.linkId), ed::PinKind::Output);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetOutPinId(t.linkId)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
            if (t.dst == nodeHash)
            {
                ImVec2 srcCenter = GetNodeCenter(t.src);
                ImVec2 offset    = CalcNormalOffset(srcCenter, myCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetInPinId(t.linkId), ed::PinKind::Input);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetInPinId(t.linkId)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
        }

        ed::EndNode();

        // push した数だけ正確に pop（スタックバランスを厳守）
        for (int i = 0; i < pushedVars; ++i)
            ed::PopStyleVar();
        for (int i = 0; i < pushedColors; ++i)
            ed::PopStyleColor();
    };

    // 各ノードの描画実行
    DrawBlockNode(ENTRY_NODE_ID, ENTRY_HASH, "Entry", ImVec4(0.15f, 0.6f, 0.15f, 1.0f), false);
    if (m_needRestorePositions)
        ed::SetNodePosition(ENTRY_NODE_ID, ImVec2(m_currentGraph.entryPosX, m_currentGraph.entryPosY));

    DrawBlockNode(ANY_STATE_NODE_ID, ANY_HASH, "Any State", ImVec4(0.1f, 0.6f, 0.6f, 1.0f), false);
    if (m_needRestorePositions)
        ed::SetNodePosition(ANY_STATE_NODE_ID, ImVec2(m_currentGraph.anyStatePosX, m_currentGraph.anyStatePosY));

    for (auto &state : m_currentGraph.states)
    {
        const bool isEntry     = (m_currentGraph.entryStateHash == state.stateHash);
        const bool isActive    = (m_runtimeActiveStateHash == state.stateHash);

        // サブマシン(節)は紫系で色分けし、名前に [Sub] を付けて「潜れる」ことを明示する。
        ImVec4 nodeBgColor;
        if (state.isSubMachine)
            nodeBgColor = ImVec4(0.32f, 0.24f, 0.48f, 1.0f);
        else
            nodeBgColor = isEntry ? ImVec4(0.9f, 0.5f, 0.1f, 1.0f)
                                  : (isActive ? ImVec4(0.2f, 0.5f, 0.8f, 1.0f)
                                              : ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

        std::string title = state.isSubMachine ? ("[Sub] " + state.sequenceName) : state.sequenceName;
        DrawBlockNode(StateNodeId(state.stateHash), state.stateHash, title.c_str(), nodeBgColor, isActive);
        if (m_needRestorePositions)
            ed::SetNodePosition(StateNodeId(state.stateHash), ImVec2(state.nodePosX, state.nodePosY));
    }

    if (m_nodeToPlaceHash != 0)
    {
        for (const auto &s : m_currentGraph.states)
        {
            if (s.stateHash == m_nodeToPlaceHash)
            {
                ed::SetNodePosition(StateNodeId(m_nodeToPlaceHash), ImVec2(s.nodePosX, s.nodePosY));
                break;
            }
        }
        ed::SelectNode(StateNodeId(m_nodeToPlaceHash));
        m_nodeToPlaceHash = 0;
    }

    // -----------------------------------------------------------------
    // 実行中のステート変化を検知して、遷移エフェクトのタイマーを発火させる
    // -----------------------------------------------------------------
    if (m_runtimeActiveStateHash != s_prevActiveState)
    {
        if (s_prevActiveState != 0 && m_runtimeActiveStateHash != 0)
        {
            s_highlightSrc = s_prevActiveState;
            s_highlightDst = m_runtimeActiveStateHash;

            // 高速遷移で残像が被らないよう、ハイライト時間を 0.3秒 に短縮
            s_highlightTimer = 0.3f;
        }
        s_prevActiveState = m_runtimeActiveStateHash;
    }
    if (s_highlightTimer > 0.0f)
    {
        s_highlightTimer -= ImGui::GetIO().DeltaTime;
        if (s_highlightTimer < 0.0f)
            s_highlightTimer = 0.0f;
    }

    // --- ④ 標準リンクの描画（NodeEditorに当たり判定と描画順を任せる） ---
    m_linkLut.clear();
    for (const auto &t : allTrans)
    {
        int srcPin = GetOutPinId(t.linkId);
        int dstPin = GetInPinId(t.linkId);

        bool isSelected = (m_selectedLinkId == t.linkId);

        // --- 遷移エフェクトの対象リンクかどうかを判定 ---
        bool isJustTransitioned = false;
        if (s_highlightTimer > 0.0f)
        {
            if (t.src == s_highlightSrc && t.dst == s_highlightDst)
            {
                isJustTransitioned = true;
            }
            else if (t.isAny && t.dst == s_highlightDst)
            {
                // 通常ステートからの遷移が存在しない場合のみ、AnyStateからの遷移として扱う
                bool hasNormal = false;
                for (const auto &tr : allTrans)
                {
                    if (!tr.isAny && tr.src == s_highlightSrc && tr.dst == s_highlightDst)
                    {
                        hasNormal = true;
                        break;
                    }
                }
                if (!hasNormal)
                    isJustTransitioned = true;
            }
        }

        // --- 状況に応じたリンクの「色」と「太さ」の決定 ---
        ImVec4 linkColor = t.isAny ? ImVec4(0.4f, 0.8f, 0.8f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);

        if (m_runtimeActiveStateHash == t.src)
        {
            linkColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // 実行中のステートから伸びる線は「青」
        }

        // 選択時、または遷移直後は「オレンジ色」にハイライト
        if (isSelected || isJustTransitioned)
        {
            linkColor = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
        }

        // 遷移直後や選択時は線を太くして視認性を上げる
        float thickness = (isSelected || isJustTransitioned) ? 4.0f : 2.5f;

        // リンクの描画
        ed::Link(t.linkId, srcPin, dstPin, linkColor, thickness);

        // 視界のノイズになる ed::Flow は完全に削除しました

        m_linkLut.push_back({t.linkId, LinkRef{t.src, t.dst, t.isAny}});

        // 矢印(三角形)の描画
        if (pinPositions.count(srcPin) && pinPositions.count(dstPin))
        {
            DrawTriangleArrow(drawList, pinPositions[srcPin], pinPositions[dstPin], ImGui::ColorConvertFloat4ToU32(linkColor));
        }
    }

    // --- ⑤ Make Transition 中のテンポラリ線描画 ---
    if (m_editorMode == EditorMode::CreatingTransition)
    {
        uint32_t srcHash = m_isTransitionSourceAnyState ? ANY_HASH : m_transitionSourceHash;

        int srcNodeId = m_isTransitionSourceAnyState ? ANY_STATE_NODE_ID
                                                    : (srcHash == ENTRY_HASH ? ENTRY_NODE_ID : StateNodeId(srcHash));

        ed::Suspend();
        ImDrawList *overlay = ImGui::GetForegroundDrawList();

        // ソースノードの矩形を「スクリーン座標」で求める。
        //   GetNodeSize はキャンバス座標サイズなので、左上(min)と右下(max)を
        //   それぞれ CanvasToScreen して引き算し、ズーム適用後の実サイズを得る。
        ImVec2 srcMinCanvas  = ed::GetNodePosition(srcNodeId);
        ImVec2 srcSizeCanvas = ed::GetNodeSize(srcNodeId);
        if (srcSizeCanvas.x <= 0.0f)
            srcSizeCanvas.x = NODE_WIDTH;
        if (srcSizeCanvas.y <= 0.0f)
            srcSizeCanvas.y = NODE_HEIGHT_ESTIMATE;

        ImVec2 srcMin = ed::CanvasToScreen(srcMinCanvas);
        ImVec2 srcMax = ed::CanvasToScreen(ImVec2(srcMinCanvas.x + srcSizeCanvas.x,
            srcMinCanvas.y + srcSizeCanvas.y));
        ImVec2 srcCenter((srcMin.x + srcMax.x) * 0.5f, (srcMin.y + srcMax.y) * 0.5f);
        ImVec2 mousePos = ImGui::GetMousePos();

        // 中心からマウス方向へ伸ばし、ノード矩形の外周との交点を始点にする。
        //   矩形と線分の交差を、方向ベクトルを矩形の半サイズでスケールして求める。
        auto EdgePoint = [](ImVec2 c, ImVec2 halfSize, ImVec2 dir) -> ImVec2 {
            // dir 方向に矩形外周まで伸ばすスケール t を求める（軸ごとに最小を取る）
            float tx = (std::abs(dir.x) > 1e-4f) ? (halfSize.x / std::abs(dir.x)) : FLT_MAX;
            float ty = (std::abs(dir.y) > 1e-4f) ? (halfSize.y / std::abs(dir.y)) : FLT_MAX;
            float t  = (std::min)(tx, ty);
            return ImVec2(c.x + dir.x * t, c.y + dir.y * t);
        };

        ImVec2 dir(mousePos.x - srcCenter.x, mousePos.y - srcCenter.y);
        float  len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len > 1e-4f)
        {
            dir.x /= len;
            dir.y /= len;
        }
        else
        {
            dir = ImVec2(1, 0);
        }

        ImVec2 srcHalf((srcMax.x - srcMin.x) * 0.5f, (srcMax.y - srcMin.y) * 0.5f);
        ImVec2 startPos = EdgePoint(srcCenter, srcHalf, dir); // 外周から出す

        // 接続先候補のハイライト：ノードにぴったり合わせた枠（スクリーン実サイズ）
        ImU32      lineColor = IM_COL32(255, 255, 255, 255);
        ImVec2     endPos    = mousePos;
        ed::NodeId hovered   = ed::GetHoveredNode();
        if (hovered.Get() != 0)
        {
            int  hid         = (int)(intptr_t)hovered.Get();
            bool validTarget = (hid != ENTRY_NODE_ID && hid != ANY_STATE_NODE_ID && hid != srcNodeId);

            // ホバーノードの矩形もスクリーン座標で正確に求める
            ImVec2 hMinCanvas  = ed::GetNodePosition(hid);
            ImVec2 hSizeCanvas = ed::GetNodeSize(hid);
            ImVec2 hMin        = ed::CanvasToScreen(hMinCanvas);
            ImVec2 hMax        = ed::CanvasToScreen(ImVec2(hMinCanvas.x + hSizeCanvas.x,
                       hMinCanvas.y + hSizeCanvas.y));

            ImU32 hl = validTarget ? IM_COL32(80, 220, 120, 255) : IM_COL32(220, 80, 80, 255);
            // ノード枠ぴったり（はみ出さないよう内側1px）
            overlay->AddRect(ImVec2(hMin.x + 1, hMin.y + 1), ImVec2(hMax.x - 1, hMax.y - 1),
                hl, 6.0f, 0, 2.5f);

            if (validTarget)
                lineColor = IM_COL32(120, 240, 160, 255);

            // 線の終点もホバーノードの外周に吸着させる（マウスではなくノード縁へ）
            ImVec2 hCenter((hMin.x + hMax.x) * 0.5f, (hMin.y + hMax.y) * 0.5f);
            ImVec2 hHalf((hMax.x - hMin.x) * 0.5f, (hMax.y - hMin.y) * 0.5f);
            ImVec2 dirToSrc(srcCenter.x - hCenter.x, srcCenter.y - hCenter.y);
            float  l2 = std::sqrt(dirToSrc.x * dirToSrc.x + dirToSrc.y * dirToSrc.y);
            if (l2 > 1e-4f)
            {
                dirToSrc.x /= l2;
                dirToSrc.y /= l2;
            }
            endPos = EdgePoint(hCenter, hHalf, dirToSrc);
        }

        overlay->AddLine(startPos, endPos, lineColor, 2.5f);
        DrawTriangleArrow(overlay, startPos, endPos, lineColor);

        ed::Resume();

        // 左クリックでターゲット確定
        // ... 以降は既存のまま

        // 左クリックでターゲット確定
        if (ImGui::IsMouseClicked(0))
        {
            ed::NodeId hoveredNode = ed::GetHoveredNode();
            if (hoveredNode.Get() != 0)
            {
                int      rawId      = (int)(intptr_t)hoveredNode.Get();
                uint32_t targetHash = 0;
                if (rawId != ENTRY_NODE_ID && rawId != ANY_STATE_NODE_ID)
                {
                    for (const auto &s : m_currentGraph.states)
                    {
                        if (StateNodeId(s.stateHash) == rawId)
                        {
                            targetHash = s.stateHash;
                            break;
                        }
                    }
                }

                if (targetHash != 0 && targetHash != srcHash)
                {
                    if (m_isTransitionSourceAnyState)
                    {
                        bool exists = false;
                        for (auto &t : m_currentGraph.anyStateTransitions)
                            if (t.targetStateHash == targetHash)
                                exists = true;
                        if (!exists)
                        {
                            AnimTransition t;
                            t.targetStateHash = targetHash;
                            m_currentGraph.anyStateTransitions.push_back(t);
                        }
                    }
                    else
                    {
                        for (auto &s : m_currentGraph.states)
                        {
                            if (s.stateHash == srcHash)
                            {
                                bool exists = false;
                                for (auto &t : s.transitions)
                                    if (t.targetStateHash == targetHash)
                                        exists = true;
                                if (!exists)
                                {
                                    AnimTransition t;
                                    t.targetStateHash = targetHash;
                                    s.transitions.push_back(t);
                                }
                                break;
                            }
                        }
                    }
                }
            }
            m_editorMode = EditorMode::Normal;
        }
        else if (ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_editorMode = EditorMode::Normal;
        }
    }

    ed::PopStyleVar(3);

    if (m_needRestorePositions)
    {
        ed::NavigateToContent();
        m_needRestorePositions = false;
    }

    HandleInteraction();
    HandleContextMenu();

    ed::End();

    // 選択状態の取得
    ed::NodeId selNode;
    ed::LinkId selLink;
    m_selectedNodeId = 0;
    m_selectedLinkId = 0;
    if (ed::GetSelectedNodes(&selNode, 1) > 0)
        m_selectedNodeId = (int)(intptr_t)selNode.Get();
    else if (ed::GetSelectedLinks(&selLink, 1) > 0)
        m_selectedLinkId = (int)(intptr_t)selLink.Get();
}

void AnimGraphWindow::HandleInteraction()
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層を編集対象にする

    // ダブルクリックしたノードがサブマシンなら、フレーム末に潜る予約をする。
    if (ed::NodeId dbl = ed::GetDoubleClickedNode())
    {
        int raw = (int)(intptr_t)dbl.Get();
        for (auto &s : m_currentGraph.states)
            if (StateNodeId(s.stateHash) == raw && s.isSubMachine) { m_pendingEnterSub = s.stateHash; break; }
    }

    if (ed::BeginDelete())
    {
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId))
        {
            if (ed::AcceptDeletedItem())
            {
                int id = (int)(intptr_t)linkId.Get();
                if (id == ENTRY_LINK_ID)
                {
                    m_currentGraph.entryStateHash = 0;
                }
                else if (const LinkRef *ref = FindLink(id))
                {
                    if (ref->isAnyState)
                    {
                        uint32_t dst = ref->dstHash;
                        m_currentGraph.anyStateTransitions.erase(
                            std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                                [dst](const AnimTransition &t) { return t.targetStateHash == dst; }),
                            m_currentGraph.anyStateTransitions.end());
                    }
                    else
                    {
                        uint32_t src = ref->srcHash, dst = ref->dstHash;
                        for (auto &s : m_currentGraph.states)
                        {
                            if (s.stateHash == src)
                            {
                                s.transitions.erase(
                                    std::remove_if(s.transitions.begin(), s.transitions.end(),
                                        [dst](const AnimTransition &t) { return t.targetStateHash == dst; }),
                                    s.transitions.end());
                                break;
                            }
                        }
                    }
                }
            }
        }

        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId))
        {
            int rawId = (int)(intptr_t)nodeId.Get();
            if (rawId == ENTRY_NODE_ID || rawId == ANY_STATE_NODE_ID)
            {
                ed::RejectDeletedItem();
                continue;
            }
            if (ed::AcceptDeletedItem())
            {
                uint32_t targetHash = 0;
                bool     found      = false;
                for (auto &s : m_currentGraph.states)
                {
                    if (StateNodeId(s.stateHash) == rawId)
                    {
                        targetHash = s.stateHash;
                        found      = true;
                        break;
                    }
                }
                if (!found)
                    continue;

                if (m_currentGraph.entryStateHash == targetHash)
                    m_currentGraph.entryStateHash = 0;

                m_currentGraph.anyStateTransitions.erase(
                    std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                        [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                    m_currentGraph.anyStateTransitions.end());

                for (auto &s : m_currentGraph.states)
                    s.transitions.erase(
                        std::remove_if(s.transitions.begin(), s.transitions.end(),
                            [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                        s.transitions.end());

                m_currentGraph.states.erase(
                    std::remove_if(m_currentGraph.states.begin(), m_currentGraph.states.end(),
                        [targetHash](const AnimState &s) { return s.stateHash == targetHash; }),
                    m_currentGraph.states.end());
            }
        }
    }
    ed::EndDelete();
}

const AnimGraphWindow::LinkRef *AnimGraphWindow::FindLink(int linkId) const
{
    for (const auto &[id, ref] : m_linkLut)
        if (id == linkId)
            return &ref;
    return nullptr;
}

void AnimGraphWindow::DrawInspector()
{
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    if (m_selectedNodeId != 0)
        DrawStateSettings(m_selectedNodeId);
    else if (m_selectedLinkId != 0)
        DrawTransitionSettings(m_selectedLinkId);
    else
        ImGui::TextDisabled("項目を選択してください。\n(Select a Node or Link)");
}

void AnimGraphWindow::DrawStateSettings(int nodeId)
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層のステートを対象にする

    if (nodeId == ENTRY_NODE_ID)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "開始地点 (Entry Node)");
        ImGui::Separator();
        ImGui::TextDisabled("ここから最初に再生したいステートへ\nワイヤーを引いて接続してください。");
        return;
    }
    if (nodeId == ANY_STATE_NODE_ID)
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "特別ノード (Any State)");
        ImGui::Separator();
        ImGui::TextDisabled("どのステートからでも遷移可能な特別遷移の起点です。\n出力ピンから各ステートへ接続してください。");
        return;
    }

    for (auto &state : m_currentGraph.states)
    {
        if (StateNodeId(state.stateHash) != nodeId)
            continue;

        char nameBuf[64];
        strncpy_s(nameBuf, sizeof(nameBuf), state.sequenceName.c_str(), _TRUNCATE);
        if (ImGui::InputText("状態名 (State Name)", nameBuf, sizeof(nameBuf)))
            state.sequenceName = nameBuf;

        ImGui::Separator();

        ImGui::Text("再生ファイル (Motion Sequence):");
        ImGui::TextDisabled("%s", state.sequenceFilePath.empty() ? "None" : state.sequenceFilePath.c_str());
        if (ImGui::Button("ファイルを選択 (Browse...)##Motion", ImVec2(-1, 0)))
        {
            if (auto path = FileDialog::OpenPreset("AnimSequence", "Select Anim Sequence"))
            {
                state.sequenceFilePath = FileDialog::ConvertToRelativePath(*path);
            }
        }

        ImGui::Separator();

        ImGui::Checkbox("ループ再生 (Loop Animation)", &state.isLoop);
        ImGui::Checkbox("ルートモーション適用 (Apply Root Motion)", &state.applyRootMotion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このステートで rootMotionCurve による前進を適用する。\nカーブは進捗→累積前進距離(m)。");
        ImGui::Checkbox("垂直方向もロック (Lock Vertical)", &state.lockVerticalRootMotion);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("ONにするとルートボーンのYも初期姿勢へ固定します。\n地上攻撃など浮いてほしくないステート向け。\nジャンプ攻撃など意図的な上下動があるステートはOFFのままにしてください。");
        ImGui::DragFloat("再生速度 (Playback Speed)", &state.playbackSpeed, 0.05f, 0.0f, 5.0f);

        ImGui::Separator();
        // 軽量サブステートマシン：グループID。同じ番号のステート群に対して、AnyState 遷移側の
        //   「Source Group」を同じ番号にすると、その群限定の割り込み遷移になる（減線）。
        {
            int gid = (int)state.groupId;
            if (ImGui::InputInt("グループID (Group ID)", &gid))
                state.groupId = (uint32_t)(gid < 0 ? 0 : gid);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("軽量サブステートマシンのグループ番号 (0=無所属)。\n"
                                  "同じ番号のステート群に対し、AnyState の遷移で『Source Group』を\n"
                                  "同じ番号にすると、その群のどの状態からでも効く割り込みになります。\n"
                                  "例: Combat 群=1 にして『Source Group=1 → Idle [Finished]』を1本置く。");
        }

        ImGui::Separator();
        if (m_currentGraph.entryStateHash == state.stateHash)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[ 初期状態 (Entry State) ]");
            ImGui::TextDisabled("※このノードが最初に再生されます。");
        }
        break;
    }
}

void AnimGraphWindow::DrawTransitionSettings(int linkId)
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層の遷移を対象にする

    if (linkId == ENTRY_LINK_ID)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "初期状態への遷移 (Entry Transition)");
        ImGui::Separator();
        ImGui::TextDisabled("※これはゲーム開始時に無条件で遷移するため、\n条件(Condition)を設定することはできません。");
        return;
    }

    const LinkRef *ref = FindLink(linkId);
    if (!ref)
        return;

    // ハッシュからライブな遷移を引き直す（ダングリング無し）
    AnimTransition *trans    = nullptr;
    std::string     fromName = "Unknown", toName = "Unknown";

    if (ref->isAnyState)
    {
        fromName = "Any State";
        for (auto &t : m_currentGraph.anyStateTransitions)
            if (t.targetStateHash == ref->dstHash)
            {
                trans = &t;
                break;
            }
    }
    else
    {
        for (auto &s : m_currentGraph.states)
        {
            if (s.stateHash == ref->srcHash)
            {
                fromName = s.sequenceName;
                for (auto &t : s.transitions)
                    if (t.targetStateHash == ref->dstHash)
                    {
                        trans = &t;
                        break;
                    }
                break;
            }
        }
    }
    if (!trans)
        return;

    for (const auto &s : m_currentGraph.states)
        if (s.stateHash == ref->dstHash)
        {
            toName = s.sequenceName;
            break;
        }

    if (ref->isAnyState)
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "特別遷移 (Any State Transition)");
    ImGui::Text("遷移元 (From): %s", fromName.c_str());
    ImGui::Text("遷移先 (To): %s", toName.c_str());
    ImGui::Separator();

    // 軽量サブステートマシン：AnyState 遷移のみ「グループ限定スコープ」を指定できる。
    //   0=どこからでも（従来）。非0にすると、現在ステートの Group ID が一致する時だけ有効。
    if (ref->isAnyState)
    {
        int sg = (int)trans->sourceGroupId;
        if (ImGui::InputInt("スコープ限定 (Source Group)", &sg))
            trans->sourceGroupId = (uint32_t)(sg < 0 ? 0 : sg);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0=どこからでも (従来の Any State)。\n"
                              "非0にすると、現在ステートの Group ID がこの番号と一致する時だけ有効な\n"
                              "『そのグループ限定の割り込み遷移』になります。");
        ImGui::Separator();
    }

    // 共通関数に一本化（通常遷移／Any State 遷移で同一UI。二重管理を解消）
    DrawTransitionConditionUI(*trans);
}


void AnimGraphWindow::DrawToolbar()
{
    if (ImGui::Button("保存 (Save As...)"))
    {
        if (auto path = FileDialog::SavePreset("AnimNode", "Save Anim Graph"))
        {
            m_currentFilePath = FileDialog::ConvertToRelativePath(*path);
            SaveGraph(m_currentFilePath);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("読込 (Load...)"))
    {
        if (auto path = FileDialog::OpenPreset("AnimNode", "Select Anim Graph File"))
        {
            m_currentFilePath = FileDialog::ConvertToRelativePath(*path);
            LoadGraph(m_currentFilePath);
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", m_currentFilePath.empty() ? "None" : m_currentFilePath.c_str());

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("全表示 (Focus All)"))
        ed::NavigateToContent();

    ImGui::SameLine();

    // =======================================================
    // 構造のテキスト化ボタン
    // =======================================================
    if (ImGui::Button("テキスト出力 (Export Text)"))
    {
        try
        {
            // 出力先のフォルダを確保
            std::filesystem::create_directories("Build/Logs");

            // ファイル名が未設定(保存前)なら "Untitled_AnimGraph" を使う
            std::string filename = m_currentFilePath.empty()
                                       ? "Untitled_AnimGraph"
                                       : std::filesystem::path(m_currentFilePath).stem().string();

            std::string txtPath  = "Build/Logs/" + filename + ".txt";
            std::string dumpText = DumpAnimGraphToString(CurrentView());

            std::ofstream txtOut(txtPath);
            if (txtOut.is_open())
            {
                txtOut << dumpText;
                CCL_LOG_INFO(LogCategory::Editor, "[Anim Editor] Exported text dump to %s", txtPath.c_str());
            }
            else
            {
                CCL_LOG_ERROR(LogCategory::Editor, "[Anim Editor] Failed to open file for text dump: %s", txtPath.c_str());
            }
        }
        catch (const std::exception &e)
        {
            CCL_LOG_WARN(LogCategory::Editor, "[Anim Editor] Failed to export text dump: %s", e.what());
        }
    }
    // =======================================================
}

void AnimGraphWindow::SaveGraph(const std::string &path)
{
    if (m_nodeContext)
    {
        ed::SetCurrentEditor(m_nodeContext);

        // ライブなノード位置は「今画面に見えている階層(view)」のノードに書き戻す。
        AnimStateGraph &view = CurrentView();

        ImVec2 entryPos  = ed::GetNodePosition(ENTRY_NODE_ID);
        view.entryPosX   = entryPos.x;
        view.entryPosY   = entryPos.y;

        for (auto &state : view.states)
        {
            ImVec2 pos     = ed::GetNodePosition(StateNodeId(state.stateHash));
            state.nodePosX = pos.x;
            state.nodePosY = pos.y;
        }

        ImVec2 anyPos    = ed::GetNodePosition(ANY_STATE_NODE_ID);
        view.anyStatePosX = anyPos.x;
        view.anyStatePosY = anyPos.y;
    }
    // 保存はツリー全体（root）を書き出す（サブマシンの中身も再帰的に含まれる）。
    AnimGraphSerializer::SaveToJSON(m_rootGraph, path);
}

void AnimGraphWindow::LoadGraph(const std::string& path) {
    if (AnimGraphSerializer::LoadFromJSON(m_rootGraph, path, {})) {
        ed::ClearSelection();
        m_editPath.clear(); // 読込後は root 表示から始める
        // ロードが成功したら、次のフレームで座標を復元するフラグを立てる
        m_needRestorePositions = true;
    }
}



void AnimGraphWindow::HandleContextMenu()
{
    AnimStateGraph &m_currentGraph = CurrentView(); // 現在の階層に対して生成/削除する

    ed::Suspend();

    static int s_contextNodeId = 0;
    ed::NodeId contextNodeId;

    // 1. ノード上での右クリック
    if (ed::ShowNodeContextMenu(&contextNodeId))
    {
        ImGui::OpenPopup("NodeContextMenu");
        s_contextNodeId = (int)(intptr_t)contextNodeId.Get();
    }
    // 2. 背景での右クリック
    else if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("AnimCanvasContextMenu");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("NodeContextMenu"))
    {
        // 対象ノードのステート名を表示（どのノードを操作しているか明示）
        uint32_t    ctxHash = 0;
        const char *ctxName = "Node";
        if (s_contextNodeId == ENTRY_NODE_ID)
            ctxName = "Entry";
        else if (s_contextNodeId == ANY_STATE_NODE_ID)
            ctxName = "Any State";
        else
        {
            for (const auto &s : m_currentGraph.states)
                if (StateNodeId(s.stateHash) == s_contextNodeId)
                {
                    ctxHash = s.stateHash;
                    ctxName = s.sequenceName.c_str();
                    break;
                }
        }
        ImGui::TextDisabled("%s", ctxName);
        ImGui::Separator();

        // --- 遷移 ---
        if (s_contextNodeId != ENTRY_NODE_ID)
        {
            if (ImGui::MenuItem("Make Transition"))
            {
                m_editorMode = EditorMode::CreatingTransition;
                if (s_contextNodeId == ANY_STATE_NODE_ID)
                {
                    m_isTransitionSourceAnyState = true;
                    m_transitionSourceHash       = 0;
                }
                else
                {
                    m_isTransitionSourceAnyState = false;
                    m_transitionSourceHash       = ctxHash;
                }
            }
        }

        // --- ステート操作（通常ノードのみ。Entry/AnyStateは対象外）---
        if (ctxHash != 0)
        {
            if (ImGui::MenuItem("Set as Entry"))
            {
                m_currentGraph.entryStateHash = ctxHash;
            }

            if (ImGui::MenuItem("Duplicate"))
            {
                // 同名+"_Copy" で複製。ハッシュは新規名から導出。
                for (const auto &s : m_currentGraph.states)
                {
                    if (s.stateHash == ctxHash)
                    {
                        AnimState copy = s;
                        copy.sequenceName += "_Copy";
                        copy.stateHash = CCL::Utils::HashString(copy.sequenceName.c_str());
                        copy.nodePosX += 30.0f;
                        copy.nodePosY += 30.0f;
                        copy.transitions.clear(); // 複製は遷移を引き継がない（重複ハッシュ事故防止）
                        m_currentGraph.states.push_back(copy);
                        m_nodeToPlaceHash = copy.stateHash;
                        break;
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
            {
                // このノードへ入る/出る遷移を全削除してからノード削除
                for (auto &s : m_currentGraph.states)
                    s.transitions.erase(
                        std::remove_if(s.transitions.begin(), s.transitions.end(),
                            [ctxHash](const AnimTransition &t) { return t.targetStateHash == ctxHash; }),
                        s.transitions.end());
                m_currentGraph.anyStateTransitions.erase(
                    std::remove_if(m_currentGraph.anyStateTransitions.begin(), m_currentGraph.anyStateTransitions.end(),
                        [ctxHash](const AnimTransition &t) { return t.targetStateHash == ctxHash; }),
                    m_currentGraph.anyStateTransitions.end());
                if (m_currentGraph.entryStateHash == ctxHash)
                    m_currentGraph.entryStateHash = 0;

                m_currentGraph.states.erase(
                    std::remove_if(m_currentGraph.states.begin(), m_currentGraph.states.end(),
                        [ctxHash](const AnimState &s) { return s.stateHash == ctxHash; }),
                    m_currentGraph.states.end());

                if (m_selectedNodeId == s_contextNodeId)
                    m_selectedNodeId = 0;
            }
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimCanvasContextMenu"))
    {
        ImGui::Text("キャンバス操作 (Canvas Actions)");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##NewStateNameCtx", m_newStateName, sizeof(m_newStateName));
        ImGui::SameLine();

        if (ImGui::Button("新しいステートを追加") && m_newStateName[0] != '\0')
        {
            std::string nm = m_newStateName;
            AnimState newState;
            newState.stateHash    = MakeUniqueStateHash(nm); // 階層全体で一意化
            newState.sequenceName = nm;

            ImVec2 canvasPos  = ed::ScreenToCanvas(m_newNodeSpawnPos);
            newState.nodePosX = canvasPos.x;
            newState.nodePosY = canvasPos.y;

            m_currentGraph.states.push_back(std::move(newState));
            // この階層にまだ Entry が無ければ、最初に置いた状態を自動で初期状態にする
            //（特に新規サブマシンは entry 未設定なので、これが無いと何も再生されない）。
            if (m_currentGraph.entryStateHash == 0)
                m_currentGraph.entryStateHash = m_currentGraph.states.back().stateHash;
            m_nodeToPlaceHash = m_currentGraph.states.back().stateHash;
            m_newStateName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        // サブステートマシン（潜れる箱）を追加：中身は空の子グラフ。
        //   生成後、そのノードをダブルクリックで潜って中身を編集する。
        if (ImGui::Button("サブステートマシンを追加") && m_newStateName[0] != '\0')
        {
            std::string nm = m_newStateName;
            AnimState sub;
            sub.stateHash    = MakeUniqueStateHash(nm);
            sub.sequenceName = nm;
            sub.isSubMachine = true;
            sub.subGraph     = std::make_unique<AnimStateGraph>(); // 空の子グラフ

            ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);
            sub.nodePosX = canvasPos.x;
            sub.nodePosY = canvasPos.y;

            m_currentGraph.states.push_back(std::move(sub));
            // 親階層にまだ Entry が無ければ、このサブマシンを初期状態にする（root が空の場合等）。
            if (m_currentGraph.entryStateHash == 0)
                m_currentGraph.entryStateHash = m_currentGraph.states.back().stateHash;
            m_nodeToPlaceHash = m_currentGraph.states.back().stateHash;
            m_newStateName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ed::Resume();
}


void AnimGraphWindow::DrawTransitionConditionUI(AnimTransition &trans)
{
    // パラメータはステートマシン全体で共有するため、ドロップダウンは常に root を参照する。
    AnimStateGraph &m_currentGraph = m_rootGraph;

    ImGui::Text("遷移設定 (Transition Settings)");
    ImGui::DragFloat("ブレンド時間 (Blend Duration)", &trans.blendDuration, 0.01f, 0.0f, 2.0f, "%.2f s");
    ImGui::Separator();

    // =========================================================================
    // ビットマスクによるキャンセル窓の要求指定 (チェックボックス化)
    // =========================================================================
    ImGui::Text("遷移を許可する窓 (Required Cancel Mask):");
    ImGui::TextDisabled("※複数チェック時は「いずれかの窓」で許可されます。");

    // ① プレビュー文字列の作成
    std::string previewMask = "";
    if (trans.requiredCancelMask == 0)
    {
        // 全てOFF（0）の場合は「常時許可」とみなす
        previewMask = "[ 常時遷移可能 (None) ]";
    }
    else
    {
        for (const auto &def : GetCancelFlagDefs())
        {
            if (trans.requiredCancelMask & def.flag)
            {
                if (!previewMask.empty()) previewMask += ", ";
                previewMask += def.name;
            }
        }
    }

    // ② ドロップダウン（コンボボックス）の描画
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##ReqCancelMask", previewMask.c_str()))
    {
        for (const auto &def : GetCancelFlagDefs())
        {
            bool active = (trans.requiredCancelMask & def.flag) != 0;
            if (ImGui::Checkbox(def.name, &active))
            {
                if (active)
                    trans.requiredCancelMask |= def.flag;
                else
                    trans.requiredCancelMask &= ~def.flag;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    int         logicOp    = (int)trans.logicType;
    const char *logicOps[] = {"すべて満たす (AND)", "いずれかを満たす (OR)"};
    if (ImGui::Combo("条件判定方法 (Logic)", &logicOp, logicOps, IM_ARRAYSIZE(logicOps)))
        trans.logicType = (AnimTransitionLogic)logicOp;
    ImGui::Separator();

    ImGui::Text("遷移条件 (Conditions)");
    if (ImGui::Button("+ 条件を追加 (Add Condition)"))
    {
        // 初期状態はエラーを防ぐため「再生終了」にしておく
        trans.conditions.push_back({0, "", AnimConditionOp::Finished, 0.0f});
    }

    // 削除するインデックスを記憶する変数（遅延削除用）
    int conditionToDelete = -1;

    for (size_t c = 0; c < trans.conditions.size(); ++c)
    {
        auto &cond = trans.conditions[c];

        ImGui::PushID((int)c);
        ImGui::Spacing();

        // =========================================================================
        // 1. Target (対象) の選択
        // =========================================================================
        std::string preview = "未選択";
        if (cond.op == AnimConditionOp::Finished)
        {
            preview = "[ 再生終了 (Finished) ]";
        }
        else if (!cond.paramName.empty())
        {
            preview = cond.paramName;
        }

        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("##Target", preview.c_str()))
        {
            // 特別な項目：再生終了
            bool isSelFinished = (cond.op == AnimConditionOp::Finished);
            if (ImGui::Selectable("[ 再生終了 (Finished) ]", isSelFinished))
            {
                cond.op        = AnimConditionOp::Finished;
                cond.paramHash = 0;
                cond.paramName = "";
            }

            ImGui::Separator();

            if (m_currentGraph.parameters.empty())
                ImGui::TextDisabled("パラメータ未登録");

            for (const auto &pd : m_currentGraph.parameters)
            {
                bool sel = (cond.paramHash == pd.hash && cond.op != AnimConditionOp::Finished);
                if (ImGui::Selectable(pd.name.c_str(), sel))
                {
                    cond.paramName = pd.name;
                    cond.paramHash = pd.hash;
                    // パラメータが変わったので、型に応じてデフォルトの演算子を自動設定する
                    if (pd.type == AnimParamType::Trigger)
                        cond.op = AnimConditionOp::Trigger;
                    else if (pd.type == AnimParamType::Bool)
                        cond.op = AnimConditionOp::Equal;
                    else
                        cond.op = AnimConditionOp::Greater; // Floatのデフォルト
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();

        // =========================================================================
        // 2. パラメータの型に応じたスマートUIの出し分け
        // =========================================================================
        if (cond.op == AnimConditionOp::Finished)
        {
            // Finished の場合は演算子も閾値も不要
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "最後まで再生で遷移");
        }
        else if (cond.paramHash != 0)
        {
            // 対象パラメータの型を検索
            AnimParamType pType = AnimParamType::Float;
            for (const auto &pd : m_currentGraph.parameters)
            {
                if (pd.hash == cond.paramHash)
                {
                    pType = pd.type;
                    break;
                }
            }

            if (pType == AnimParamType::Trigger)
            {
                // Trigger は演算子も閾値も不要
                cond.op = AnimConditionOp::Trigger;
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "発動で遷移");
            }
            else if (pType == AnimParamType::Bool)
            {
                // Bool は == に固定し、true/false のドロップダウンに変更
                cond.op = AnimConditionOp::Equal;
                ImGui::Text(" は ");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);

                int         boolVal       = (cond.threshold > 0.5f) ? 1 : 0;
                const char *boolOptions[] = {"false", "true"};
                if (ImGui::Combo("##BoolVal", &boolVal, boolOptions, 2))
                {
                    cond.threshold = boolVal ? 1.0f : 0.0f;
                }
            }
            else if (pType == AnimParamType::Float)
            {
                // Float の場合は無意味な演算子を排除
                if (cond.op == AnimConditionOp::Trigger || cond.op == AnimConditionOp::Finished)
                    cond.op = AnimConditionOp::Greater;

                int         opInt      = (int)cond.op; // 0:Equal, 1:Greater, 2:Less
                const char *opsFloat[] = {"==", ">", "<"};

                ImGui::SetNextItemWidth(50);
                if (ImGui::Combo("##FloatOp", &opInt, opsFloat, 3))
                    cond.op = (AnimConditionOp)opInt;

                ImGui::SameLine();
                ImGui::SetNextItemWidth(60);
                ImGui::DragFloat("##FloatVal", &cond.threshold, 0.05f);
            }
        }
        else
        {
            ImGui::TextDisabled("対象を選択...");
        }

        // 行の右端に削除ボタン
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(24, 0)))
        {
            conditionToDelete = (int)c;
        }

        ImGui::PopID();
    }

    // 遅延削除の実行
    if (conditionToDelete >= 0)
    {
        trans.conditions.erase(trans.conditions.begin() + conditionToDelete);
    }
}


// ===========================================================================
// パラメータパネル（Unity Animator の Parameters タブ相当）
//   グラフが所有する AnimParamDef を一覧編集する。実行中は選択エンティティの
//   AnimParametersComponent から現在値を読み、ライブ表示する。
// ===========================================================================
void AnimGraphWindow::DrawParameterPanel(EditorContext &context)
{
    // パラメータはステートマシン全体で共有（root が唯一の所有者）。
    AnimStateGraph &m_currentGraph = m_rootGraph;

    ImGui::Text("パラメータ (Parameters)");
    ImGui::SameLine();

    // --- 追加ボタン（型ごと） ---
    if (ImGui::SmallButton("+ Float"))
    {
        AnimParamDef p;
        p.name = "NewFloat";
        p.type = AnimParamType::Float;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Bool"))
    {
        AnimParamDef p;
        p.name = "NewBool";
        p.type = AnimParamType::Bool;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Trigger"))
    {
        AnimParamDef p;
        p.name = "NewTrigger";
        p.type = AnimParamType::Trigger;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        m_currentGraph.parameters.push_back(p);
    }

    ImGui::Separator();

    // --- ライブ値参照用（実行中エンティティの実値） ---
    AnimParametersComponent *liveParams = nullptr;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world)
        liveParams = context.world->GetComponent<AnimParametersComponent>(context.selectedEntity);

    if (!ImGui::BeginTable("ParamTable", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
        return;

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type/Default", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f); // 削除ボタン列
    ImGui::TableHeadersRow();

    int deleteIndex = -1;

    for (int i = 0; i < (int)m_currentGraph.parameters.size(); ++i)
    {
        AnimParamDef &p = m_currentGraph.parameters[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();

        // --- 列1: 名前（ダブルクリックでリネーム編集） ---
        ImGui::TableSetColumnIndex(0);
        if (m_paramRenameIndex == i)
        {
            ImGui::SetNextItemWidth(-1);
            bool commit = ImGui::InputText("##rename", m_paramNameBuf, sizeof(m_paramNameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (commit || ImGui::IsItemDeactivatedAfterEdit())
            {
                std::string newName = m_paramNameBuf;
                if (!newName.empty() && newName != p.name)
                {
                    uint32_t oldHash = p.hash;
                    uint32_t newHash = CCL::Utils::HashString(newName.c_str());
                    // 参照している全条件を張り替える（これを怠ると条件が壊れる）
                    RenameParameterReferences(oldHash, newName, newHash);
                    p.name = newName;
                    p.hash = newHash;
                }
                m_paramRenameIndex = -1;
            }
        }
        else
        {
            // アイコン的に型を色分け表示
            ImVec4 col = (p.type == AnimParamType::Float)  ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f)
                         : (p.type == AnimParamType::Bool) ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f)
                                                           : ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
            ImGui::TextColored(col, "%s", p.name.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_paramRenameIndex = i;
                strncpy_s(m_paramNameBuf, sizeof(m_paramNameBuf), p.name.c_str(), _TRUNCATE);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("ID: 0x%08X\n(ダブルクリックでリネーム)", p.hash);
        }

        // --- 列2: 型とデフォルト値 ---
        ImGui::TableSetColumnIndex(1);
        switch (p.type)
        {
        case AnimParamType::Float:
            ImGui::SetNextItemWidth(-1);
            ImGui::DragFloat("##def", &p.defaultFloat, 0.05f);
            break;
        case AnimParamType::Bool:
            ImGui::Checkbox("##def", &p.defaultBool);
            ImGui::SameLine();
            ImGui::TextDisabled("Bool");
            break;
        case AnimParamType::Trigger:
            ImGui::TextDisabled("Trigger");
            break;
        }

        // --- 列3: ライブ値（実行中の実値） ---
        ImGui::TableSetColumnIndex(2);
        if (liveParams)
        {
            float fv = 0.0f;
            if (liveParams->TryGetNumeric(p.hash, fv))
            {
                if (p.type == AnimParamType::Bool || p.type == AnimParamType::Trigger)
                    ImGui::TextColored(fv != 0.0f ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        fv != 0.0f ? "TRUE" : "false");
                else
                    ImGui::Text("%.2f", fv);
            }
            else
                ImGui::TextDisabled("-");
        }
        else
            ImGui::TextDisabled("-");

        // --- 列4: 削除ボタン（明示的に見える形で） ---
        ImGui::TableSetColumnIndex(3);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
        if (ImGui::SmallButton("×"))
            deleteIndex = i;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("このパラメータを削除");

        // 右クリックでも削除メニューを出す（従来動作の互換）
        if (ImGui::BeginPopupContextItem("paramCtx"))
        {
            if (ImGui::MenuItem("削除 (Delete)"))
                deleteIndex = i;
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::EndTable();

    // ループ外で削除（イテレータ破壊回避）
    if (deleteIndex >= 0)
    {
        // 消すパラメータを参照している遷移条件も一緒に除去する。
        // これを怠ると、存在しないハッシュを参照する「壊れた条件」が残る。
        const uint32_t deadHash = m_currentGraph.parameters[deleteIndex].hash;
        auto stripConds = [deadHash](std::vector<AnimTransition> &transitions) {
            for (auto &t : transitions)
                t.conditions.erase(
                    std::remove_if(t.conditions.begin(), t.conditions.end(),
                        [deadHash](const AnimCondition &c) { return c.paramHash == deadHash; }),
                    t.conditions.end());
        };
        // 階層のどこにある条件も漏れなく除去（root＋全サブグラフ）。
        ForEachTransitionList(m_rootGraph, stripConds);

        m_currentGraph.parameters.erase(m_currentGraph.parameters.begin() + deleteIndex);
    }
}

// パラメータ名変更時、全遷移・AnyState遷移の条件参照を張り替える。
void AnimGraphWindow::RenameParameterReferences(uint32_t oldHash, const std::string &newName, uint32_t newHash)
{
    auto patch = [&](std::vector<AnimTransition> &transitions) {
        for (auto &t : transitions)
            for (auto &c : t.conditions)
                if (c.paramHash == oldHash)
                {
                    c.paramHash = newHash;
                    c.paramName = newName;
                }
    };
    // 階層のどこにある条件も漏れなく張り替える（root＋全サブグラフ）。
    ForEachTransitionList(m_rootGraph, patch);
}

