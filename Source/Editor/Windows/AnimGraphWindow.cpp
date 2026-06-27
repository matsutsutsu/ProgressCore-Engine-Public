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
//#include "Engine/Graphics/RHI/Graphics.h"
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
    inline int StateInPin (uint32_t h) { return (int)(h * 4u + 1u); }
    inline int StateOutPin(uint32_t h) { return (int)(h * 4u + 2u); }

    // 予約ID（すべて mod4 == 3。ノード/ピンのスロット0,1,2と交差しない）
    constexpr int ENTRY_NODE_ID     = -1;   // 0xFFFFFFFF & 3 == 3
    constexpr int ENTRY_OUT_PIN     = -5;   // & 3 == 3
    constexpr int ENTRY_LINK_ID     = -9;   // & 3 == 3
    constexpr int ANY_STATE_NODE_ID = -13;  // & 3 == 3
    constexpr int ANY_STATE_OUT_PIN = -17;  // & 3 == 3

    // リンクID。素性解決は _linkLut で行うため、ここは「同フレーム内で一意」であれば足りる。
    constexpr int NORMAL_LINK_BASE = 0x00100000;
    constexpr int ANY_LINK_BASE    = 0x00200000;
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

    // ★ 矢印を線の真ん中ではなく、ターゲットノードの「直前（80px手前）」に配置する (Unity風)
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
    _nodeContext = ed::CreateEditor();
}

AnimGraphWindow::~AnimGraphWindow() {
    if (_nodeContext) ed::DestroyEditor(_nodeContext);
}

void AnimGraphWindow::DrawContents(EditorContext& context) {
    ed::SetCurrentEditor(_nodeContext);

    // ================================================================
    // リアルタイム・デバッグ (選択中エンティティのステート同期)
    // ================================================================
    _runtimeActiveStateHash = 0;
    // エンティティが選択されており、ワールドが存在する場合
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world) {
        auto* fsm = context.world->GetComponent<AnimStateMachineComponent>(context.selectedEntity);
        if (fsm) {
            // 現在のステートハッシュを記憶する
            _runtimeActiveStateHash = fsm->currentStateHash;
        }
    }

    DrawToolbar();

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
    const float paramPanelHeight = ImGui::GetContentRegionAvail().y * 0.4f;

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
    ed::Begin("My Anim Node Editor");

    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f); // 直線
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 12.0f);
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));

    constexpr float NODE_WIDTH           = 150.0f;
    constexpr float NODE_HEIGHT_ESTIMATE = 42.0f; // ノードの概算高さ

    std::unordered_map<int, ImVec2> pinPositions;
    ImDrawList                     *drawList = ImGui::GetWindowDrawList();

    // --- ① 安定したピンIDを動的発行するヘルパー ---
    auto GetOutPinId = [](uint32_t src, uint32_t dst) -> int {
        uint64_t combined = ((uint64_t)src << 32) | (uint64_t)dst;
        return (int)((std::hash<uint64_t>{}(combined) & 0x3FFFFFFF) | 0x40000000);
    };
    auto GetInPinId = [](uint32_t src, uint32_t dst) -> int {
        uint64_t combined = ((uint64_t)src << 32) | (uint64_t)dst;
        return (int)((std::hash<uint64_t>{}(combined) & 0x3FFFFFFF) | 0x10000000);
    };

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

    if (_currentGraph.entryStateHash != 0)
        allTrans.push_back({ENTRY_HASH, _currentGraph.entryStateHash, false, ENTRY_LINK_ID});

    for (const auto &trans : _currentGraph.anyStateTransitions)
        allTrans.push_back({ANY_HASH, trans.targetStateHash, true, ANY_LINK_BASE + (linkIdCounter++)});

    for (const auto &s : _currentGraph.states)
    {
        for (const auto &trans : s.transitions)
        {
            allTrans.push_back({s.stateHash, trans.targetStateHash, false, linkIdCounter++});
        }
    }

    // キャンバス座標系でのノード中心を取得
    auto GetNodeCenter = [&](uint32_t hash) -> ImVec2 {
        if (hash == ENTRY_HASH)
            return ImVec2(_currentGraph.entryPosX + NODE_WIDTH / 2, _currentGraph.entryPosY + NODE_HEIGHT_ESTIMATE / 2);
        if (hash == ANY_HASH)
            return ImVec2(_currentGraph.anyStatePosX + NODE_WIDTH / 2, _currentGraph.anyStatePosY + NODE_HEIGHT_ESTIMATE / 2);
        for (const auto &s : _currentGraph.states)
        {
            if (s.stateHash == hash)
                return ImVec2(s.nodePosX + NODE_WIDTH / 2, s.nodePosY + NODE_HEIGHT_ESTIMATE / 2);
        }
        return ImVec2(0, 0);
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
        ed::PushStyleColor(ed::StyleColor_NodeBg, bgColor);
        if (isActive)
        {
            float pulse = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.4f + pulse * 0.4f, 0.8f + pulse * 0.2f, 1.0f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.0f + pulse * 1.5f);
        }

        ed::BeginNode(nodeId);

        ImVec2 startPos  = ImGui::GetCursorPos();       // NodeLocal
        ImVec2 screenPos = ImGui::GetCursorScreenPos(); // Screen

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

        // ★ 自分が関与する全遷移について、法線にずらしたピンを動的に生成する
        for (const auto &t : allTrans)
        {
            if (t.src == nodeHash)
            { // 自分から出る遷移 (Out)
                ImVec2 targetCenter = GetNodeCenter(t.dst);
                ImVec2 offset       = CalcNormalOffset(myCenter, targetCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetOutPinId(t.src, t.dst), ed::PinKind::Output);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetOutPinId(t.src, t.dst)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
            if (t.dst == nodeHash)
            { // 自分へ入る遷移 (In)
                ImVec2 srcCenter = GetNodeCenter(t.src);
                ImVec2 offset    = CalcNormalOffset(srcCenter, myCenter);

                ImVec2 pinLocal = ImVec2(startPos.x + NODE_WIDTH / 2 + offset.x, startPos.y + nodeHeight / 2 + offset.y);
                ImGui::SetCursorPos(pinLocal);
                ed::BeginPin(GetInPinId(t.src, t.dst), ed::PinKind::Input);
                ImGui::Dummy(ImVec2(1, 1));
                ed::EndPin();
                pinPositions[GetInPinId(t.src, t.dst)] = ImVec2(screenPos.x + NODE_WIDTH / 2 + offset.x, screenPos.y + nodeHeight / 2 + offset.y);
            }
        }

        ed::EndNode();

        if (isActive)
        {
            ed::PopStyleVar();
            ed::PopStyleColor();
        }
        ed::PopStyleColor();
    };

    // 各ノードの描画実行
    DrawBlockNode(ENTRY_NODE_ID, ENTRY_HASH, "Entry", ImVec4(0.15f, 0.6f, 0.15f, 1.0f), false);
    if (_needRestorePositions)
        ed::SetNodePosition(ENTRY_NODE_ID, ImVec2(_currentGraph.entryPosX, _currentGraph.entryPosY));

    DrawBlockNode(ANY_STATE_NODE_ID, ANY_HASH, "Any State", ImVec4(0.1f, 0.6f, 0.6f, 1.0f), false);
    if (_needRestorePositions)
        ed::SetNodePosition(ANY_STATE_NODE_ID, ImVec2(_currentGraph.anyStatePosX, _currentGraph.anyStatePosY));

    for (auto &state : _currentGraph.states)
    {
        const bool isEntry     = (_currentGraph.entryStateHash == state.stateHash);
        const bool isActive    = (_runtimeActiveStateHash == state.stateHash);
        ImVec4     nodeBgColor = isEntry ? ImVec4(0.9f, 0.5f, 0.1f, 1.0f) : (isActive ? ImVec4(0.2f, 0.5f, 0.8f, 1.0f) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f));

        DrawBlockNode(StateNodeId(state.stateHash), state.stateHash, state.sequenceName.c_str(), nodeBgColor, isActive);
        if (_needRestorePositions)
            ed::SetNodePosition(StateNodeId(state.stateHash), ImVec2(state.nodePosX, state.nodePosY));
    }

    if (_nodeToPlaceHash != 0)
    {
        for (const auto &s : _currentGraph.states)
        {
            if (s.stateHash == _nodeToPlaceHash)
            {
                ed::SetNodePosition(StateNodeId(_nodeToPlaceHash), ImVec2(s.nodePosX, s.nodePosY));
                break;
            }
        }
        ed::SelectNode(StateNodeId(_nodeToPlaceHash));
        _nodeToPlaceHash = 0;
    }

    // -----------------------------------------------------------------
    // ★ 実行中のステート変化を検知して、遷移エフェクトのタイマーを発火させる
    // -----------------------------------------------------------------
    if (_runtimeActiveStateHash != s_prevActiveState)
    {
        if (s_prevActiveState != 0 && _runtimeActiveStateHash != 0)
        {
            s_highlightSrc = s_prevActiveState;
            s_highlightDst = _runtimeActiveStateHash;

            // ★ 高速遷移で残像が被らないよう、ハイライト時間を 0.3秒 に短縮
            s_highlightTimer = 0.3f;
        }
        s_prevActiveState = _runtimeActiveStateHash;
    }
    if (s_highlightTimer > 0.0f)
    {
        s_highlightTimer -= ImGui::GetIO().DeltaTime;
        if (s_highlightTimer < 0.0f)
            s_highlightTimer = 0.0f;
    }

    // --- ④ 標準リンクの描画（NodeEditorに当たり判定と描画順を任せる） ---
    _linkLut.clear();
    for (const auto &t : allTrans)
    {
        int srcPin = GetOutPinId(t.src, t.dst);
        int dstPin = GetInPinId(t.src, t.dst);

        bool isSelected = (_selectedLinkId == t.linkId);

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

        if (_runtimeActiveStateHash == t.src)
        {
            linkColor = ImVec4(0.3f, 0.7f, 1.0f, 1.0f); // 実行中のステートから伸びる線は「青」
        }

        // ★ 選択時、または遷移直後は「オレンジ色」にハイライト
        if (isSelected || isJustTransitioned)
        {
            linkColor = ImVec4(1.0f, 0.65f, 0.0f, 1.0f);
        }

        // 遷移直後や選択時は線を太くして視認性を上げる
        float thickness = (isSelected || isJustTransitioned) ? 4.0f : 2.5f;

        // リンクの描画
        ed::Link(t.linkId, srcPin, dstPin, linkColor, thickness);

        // ★ 視界のノイズになる ed::Flow は完全に削除しました

        _linkLut.push_back({t.linkId, LinkRef{t.src, t.dst, t.isAny}});

        // 矢印(三角形)の描画
        if (pinPositions.count(srcPin) && pinPositions.count(dstPin))
        {
            DrawTriangleArrow(drawList, pinPositions[srcPin], pinPositions[dstPin], ImGui::ColorConvertFloat4ToU32(linkColor));
        }
    }

    // --- ⑤ Make Transition 中のテンポラリ線描画 ---
    if (_editorMode == EditorMode::CreatingTransition)
    {
        ImVec2   mousePos = ImGui::GetMousePos();
        uint32_t srcHash  = _isTransitionSourceAnyState ? ANY_HASH : _transitionSourceHash;

        // 始点をキャンバス座標からスクリーン座標へ変換
        ImVec2 startPos = ed::CanvasToScreen(GetNodeCenter(srcHash));

        drawList->AddLine(startPos, mousePos, IM_COL32(255, 255, 255, 255), 2.5f);
        DrawTriangleArrow(drawList, startPos, mousePos, IM_COL32(255, 255, 255, 255));

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
                    for (const auto &s : _currentGraph.states)
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
                    if (_isTransitionSourceAnyState)
                    {
                        bool exists = false;
                        for (auto &t : _currentGraph.anyStateTransitions)
                            if (t.targetStateHash == targetHash)
                                exists = true;
                        if (!exists)
                        {
                            AnimTransition t;
                            t.targetStateHash = targetHash;
                            _currentGraph.anyStateTransitions.push_back(t);
                        }
                    }
                    else
                    {
                        for (auto &s : _currentGraph.states)
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
            _editorMode = EditorMode::Normal;
        }
        else if (ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            _editorMode = EditorMode::Normal;
        }
    }

    ed::PopStyleVar(3);

    if (_needRestorePositions)
    {
        ed::NavigateToContent();
        _needRestorePositions = false;
    }

    HandleInteraction();
    HandleContextMenu();

    ed::End();

    // 選択状態の取得
    ed::NodeId selNode;
    ed::LinkId selLink;
    _selectedNodeId = 0;
    _selectedLinkId = 0;
    if (ed::GetSelectedNodes(&selNode, 1) > 0)
        _selectedNodeId = (int)(intptr_t)selNode.Get();
    else if (ed::GetSelectedLinks(&selLink, 1) > 0)
        _selectedLinkId = (int)(intptr_t)selLink.Get();
}

void AnimGraphWindow::HandleInteraction()
{
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
                    _currentGraph.entryStateHash = 0;
                }
                else if (const LinkRef *ref = FindLink(id))
                {
                    if (ref->isAnyState)
                    {
                        uint32_t dst = ref->dstHash;
                        _currentGraph.anyStateTransitions.erase(
                            std::remove_if(_currentGraph.anyStateTransitions.begin(), _currentGraph.anyStateTransitions.end(),
                                [dst](const AnimTransition &t) { return t.targetStateHash == dst; }),
                            _currentGraph.anyStateTransitions.end());
                    }
                    else
                    {
                        uint32_t src = ref->srcHash, dst = ref->dstHash;
                        for (auto &s : _currentGraph.states)
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
                for (auto &s : _currentGraph.states)
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

                if (_currentGraph.entryStateHash == targetHash)
                    _currentGraph.entryStateHash = 0;

                _currentGraph.anyStateTransitions.erase(
                    std::remove_if(_currentGraph.anyStateTransitions.begin(), _currentGraph.anyStateTransitions.end(),
                        [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                    _currentGraph.anyStateTransitions.end());

                for (auto &s : _currentGraph.states)
                    s.transitions.erase(
                        std::remove_if(s.transitions.begin(), s.transitions.end(),
                            [targetHash](const AnimTransition &t) { return t.targetStateHash == targetHash; }),
                        s.transitions.end());

                _currentGraph.states.erase(
                    std::remove_if(_currentGraph.states.begin(), _currentGraph.states.end(),
                        [targetHash](const AnimState &s) { return s.stateHash == targetHash; }),
                    _currentGraph.states.end());
            }
        }
    }
    ed::EndDelete();
}

const AnimGraphWindow::LinkRef *AnimGraphWindow::FindLink(int linkId) const
{
    for (const auto &[id, ref] : _linkLut)
        if (id == linkId)
            return &ref;
    return nullptr;
}

void AnimGraphWindow::DrawInspector()
{
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    if (_selectedNodeId != 0)
        DrawStateSettings(_selectedNodeId);
    else if (_selectedLinkId != 0)
        DrawTransitionSettings(_selectedLinkId);
    else
        ImGui::TextDisabled("項目を選択してください。\n(Select a Node or Link)");
}

void AnimGraphWindow::DrawStateSettings(int nodeId)
{
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

    for (auto &state : _currentGraph.states)
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
        ImGui::DragFloat("再生速度 (Playback Speed)", &state.playbackSpeed, 0.05f, 0.0f, 5.0f);

        char curveBuf[128];
        strncpy_s(curveBuf, sizeof(curveBuf), state.speedCurveName.c_str(), _TRUNCATE);
        if (ImGui::InputText("速度カーブ (Speed Curve)", curveBuf, sizeof(curveBuf)))
            state.speedCurveName = curveBuf;

        char rmCurveBuf[256];
        strncpy_s(rmCurveBuf, sizeof(rmCurveBuf), state.rootMotionCurveName.c_str(), _TRUNCATE);
        if (ImGui::InputText("ルートモーションカーブ (RootMotion Curve)", rmCurveBuf, sizeof(rmCurveBuf)))
            state.rootMotionCurveName = rmCurveBuf;

        ImGui::Separator();
        if (_currentGraph.entryStateHash == state.stateHash)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[ 初期状態 (Entry State) ]");
            ImGui::TextDisabled("※このノードが最初に再生されます。");
        }
        break;
    }
}

void AnimGraphWindow::DrawTransitionSettings(int linkId)
{
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
        for (auto &t : _currentGraph.anyStateTransitions)
            if (t.targetStateHash == ref->dstHash)
            {
                trans = &t;
                break;
            }
    }
    else
    {
        for (auto &s : _currentGraph.states)
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

    for (const auto &s : _currentGraph.states)
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

    // ★ 共通関数に一本化（通常遷移／Any State 遷移で同一UI。二重管理を解消）
    DrawTransitionConditionUI(*trans);
}


void AnimGraphWindow::DrawToolbar()
{
    if (ImGui::Button("保存 (Save As...)"))
    {
        if (auto path = FileDialog::SavePreset("AnimNode", "Save Anim Graph"))
        {
            _currentFilePath = FileDialog::ConvertToRelativePath(*path);
            SaveGraph(_currentFilePath);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("読込 (Load...)"))
    {
        if (auto path = FileDialog::OpenPreset("AnimNode", "Select Anim Graph File"))
        {
            _currentFilePath = FileDialog::ConvertToRelativePath(*path);
            LoadGraph(_currentFilePath);
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", _currentFilePath.empty() ? "None" : _currentFilePath.c_str());

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("全表示 (Focus All)"))
        ed::NavigateToContent();
}

void AnimGraphWindow::SaveGraph(const std::string &path)
{
    if (_nodeContext)
    {
        ed::SetCurrentEditor(_nodeContext);

        ImVec2 entryPos         = ed::GetNodePosition(ENTRY_NODE_ID);
        _currentGraph.entryPosX = entryPos.x;
        _currentGraph.entryPosY = entryPos.y;

        for (auto &state : _currentGraph.states)
        {
            ImVec2 pos     = ed::GetNodePosition(StateNodeId(state.stateHash));
            state.nodePosX = pos.x;
            state.nodePosY = pos.y;
        }

        ImVec2 anyPos              = ed::GetNodePosition(ANY_STATE_NODE_ID);
        _currentGraph.anyStatePosX = anyPos.x;
        _currentGraph.anyStatePosY = anyPos.y;
    }
    AnimGraphSerializer::SaveToJSON(_currentGraph, path);

    // ノードグラフをテキスト化にしてわかりやすくする
    #if 0
    try
    {
        std::filesystem::create_directories("Build/Logs");
        std::string   filename = std::filesystem::path(path).stem().string();
        std::string   txtPath  = "Build/Logs/" + filename + ".txt";
        std::string   dumpText = DumpAnimGraphToString(_currentGraph);
        std::ofstream txtOut(txtPath);
        if (txtOut.is_open())
        {
            txtOut << dumpText;
            CCL_LOG_INFO(LogCategory::Editor, "[Anim Editor] Exported text dump to %s", txtPath.c_str());
        }
    }
    catch (const std::exception &e)
    {
        CCL_LOG_WARN(LogCategory::Editor, "[Anim Editor] Failed to export text dump: %s", e.what());
    }
    #endif
}

void AnimGraphWindow::LoadGraph(const std::string& path) {
    if (AnimGraphSerializer::LoadFromJSON(_currentGraph, path, {})) {
        ed::ClearSelection();
        // ★ ロードが成功したら、次のフレームで座標を復元するフラグを立てる
        _needRestorePositions = true;
    }
}



void AnimGraphWindow::HandleContextMenu()
{
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
        _newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("NodeContextMenu"))
    {
        if (s_contextNodeId != ENTRY_NODE_ID)
        {
            if (ImGui::MenuItem("Make Transition"))
            {
                _editorMode = EditorMode::CreatingTransition;
                if (s_contextNodeId == ANY_STATE_NODE_ID)
                {
                    _isTransitionSourceAnyState = true;
                    _transitionSourceHash       = 0;
                }
                else
                {
                    _isTransitionSourceAnyState = false;
                    for (const auto &s : _currentGraph.states)
                    {
                        if (StateNodeId(s.stateHash) == s_contextNodeId)
                        {
                            _transitionSourceHash = s.stateHash;
                            break;
                        }
                    }
                }
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("AnimCanvasContextMenu"))
    {
        ImGui::Text("キャンバス操作 (Canvas Actions)");
        ImGui::Separator();

        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##NewStateNameCtx", _newStateName, sizeof(_newStateName));
        ImGui::SameLine();

        if (ImGui::Button("新しいステートを追加") && _newStateName[0] != '\0')
        {
            AnimState newState;
            newState.stateHash    = CCL::Utils::HashString(_newStateName);
            newState.sequenceName = _newStateName;

            ImVec2 canvasPos  = ed::ScreenToCanvas(_newNodeSpawnPos);
            newState.nodePosX = canvasPos.x;
            newState.nodePosY = canvasPos.y;

            _currentGraph.states.push_back(newState);
            _nodeToPlaceHash = newState.stateHash;
            _newStateName[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ed::Resume();
}


void AnimGraphWindow::DrawTransitionConditionUI(AnimTransition &trans)
{
    ImGui::Text("遷移設定 (Transition Settings)");
    ImGui::DragFloat("ブレンド時間 (Blend Duration)", &trans.blendDuration, 0.01f, 0.0f, 2.0f, "%.2f s");
    ImGui::Separator();

    // --- 受付ウィンドウ指定（コンボ/キャンセル区間に紐付け） ---
    {
        char winBuf[64];
        strncpy_s(winBuf, sizeof(winBuf), trans.requiredWindow.c_str(), _TRUNCATE);
        ImGui::Text("受付ウィンドウ (Required Window):");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##ReqWin", winBuf, sizeof(winBuf)))
        {
            trans.requiredWindow     = winBuf;
            trans.requiredWindowHash = (winBuf[0] == '\0') ? 0u
                                                           : CCL::Utils::HashString(winBuf);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sequencer の Combo/Cancel Window 名を指定。\n空なら常時遷移可。区間内のみ遷移を許可。");
    }
    ImGui::Separator();

    int         logicOp    = (int)trans.logicType;
    const char *logicOps[] = {"すべて満たす (AND)", "いずれかを満たす (OR)"};
    if (ImGui::Combo("判定方法 (Logic)", &logicOp, logicOps, IM_ARRAYSIZE(logicOps)))
        trans.logicType = (AnimTransitionLogic)logicOp;
    ImGui::Separator();

    ImGui::Text("遷移条件 (Conditions)");
    if (ImGui::Button("+ 条件を追加 (Add Condition)"))
    {
        trans.conditions.push_back({CCL::Utils::HashString("Speed"), "Speed", AnimConditionOp::Greater, 0.0f});
    }

    // ★ 削除するインデックスを記憶する変数（遅延削除用）
    int conditionToDelete = -1;

    for (size_t c = 0; c < trans.conditions.size(); ++c)
    {
        auto &cond = trans.conditions[c];

        ImGui::PushID((int)c); // ★ PushID
        ImGui::Separator();
        ImGui::Text("条件 (Condition) %d", (int)c);
        ImGui::Text("対象パラメータ (Parameter):");
        ImGui::SetNextItemWidth(-1);

        const char *preview = cond.paramName.empty() ? "(未選択)" : cond.paramName.c_str();
        if (ImGui::BeginCombo("##ParamSelect", preview))
        {
            if (_currentGraph.parameters.empty())
                ImGui::TextDisabled("パラメータ未登録。\n左のパネルで追加してください。");
            for (const auto &pd : _currentGraph.parameters)
            {
                bool sel = (cond.paramHash == pd.hash);
                if (ImGui::Selectable(pd.name.c_str(), sel))
                {
                    cond.paramName = pd.name;
                    cond.paramHash = pd.hash;
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("ID: 0x%08X", cond.paramHash);

        int op = (int)cond.op;
        ImGui::SetNextItemWidth(150);
        const char *ops[] = {"一致 (Equal)", "より大きい (Greater)", "より小さい (Less)", "トリガー (Trigger)", "再生終了 (Finished)"};
        if (ImGui::Combo("条件 (Op)", &op, ops, IM_ARRAYSIZE(ops)))
            cond.op = (AnimConditionOp)op;

        if (cond.op != AnimConditionOp::Trigger && cond.op != AnimConditionOp::Finished)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::DragFloat("閾値 (Value)", &cond.threshold, 0.05f);
        }
        else
        {
            cond.threshold = 0.0f;
        }

        // ★ 即座に erase せず、インデックスを記憶するだけにする
        if (ImGui::Button("削除 (Delete)"))
        {
            conditionToDelete = (int)c;
        }

        ImGui::PopID(); // ★ 確実にペアになる PopID
    }

    // ★ ループを安全に抜けた後で配列から削除する
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
    ImGui::Text("パラメータ (Parameters)");
    ImGui::SameLine();

    // --- 追加ボタン（型ごと） ---
    if (ImGui::SmallButton("+ Float"))
    {
        AnimParamDef p;
        p.name = "NewFloat";
        p.type = AnimParamType::Float;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        _currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Bool"))
    {
        AnimParamDef p;
        p.name = "NewBool";
        p.type = AnimParamType::Bool;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        _currentGraph.parameters.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Trigger"))
    {
        AnimParamDef p;
        p.name = "NewTrigger";
        p.type = AnimParamType::Trigger;
        p.hash = CCL::Utils::HashString(p.name.c_str());
        _currentGraph.parameters.push_back(p);
    }

    ImGui::Separator();

    // --- ライブ値参照用（実行中エンティティの実値） ---
    AnimParametersComponent *liveParams = nullptr;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world)
        liveParams = context.world->GetComponent<AnimParametersComponent>(context.selectedEntity);

    if (!ImGui::BeginTable("ParamTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY))
        return;

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Type/Default", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    int deleteIndex = -1;

    for (int i = 0; i < (int)_currentGraph.parameters.size(); ++i)
    {
        AnimParamDef &p = _currentGraph.parameters[i];
        ImGui::PushID(i);
        ImGui::TableNextRow();

        // --- 列1: 名前（ダブルクリックでリネーム編集） ---
        ImGui::TableSetColumnIndex(0);
        if (_paramRenameIndex == i)
        {
            ImGui::SetNextItemWidth(-1);
            bool commit = ImGui::InputText("##rename", _paramNameBuf, sizeof(_paramNameBuf),
                ImGuiInputTextFlags_EnterReturnsTrue);
            if (commit || ImGui::IsItemDeactivatedAfterEdit())
            {
                std::string newName = _paramNameBuf;
                if (!newName.empty() && newName != p.name)
                {
                    uint32_t oldHash = p.hash;
                    uint32_t newHash = CCL::Utils::HashString(newName.c_str());
                    // 参照している全条件を張り替える（これを怠ると条件が壊れる）
                    RenameParameterReferences(oldHash, newName, newHash);
                    p.name = newName;
                    p.hash = newHash;
                }
                _paramRenameIndex = -1;
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
                _paramRenameIndex = i;
                strncpy_s(_paramNameBuf, sizeof(_paramNameBuf), p.name.c_str(), _TRUNCATE);
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

        // 右クリックで削除メニュー
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
        _currentGraph.parameters.erase(_currentGraph.parameters.begin() + deleteIndex);
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
    for (auto &s : _currentGraph.states)
        patch(s.transitions);
    patch(_currentGraph.anyStateTransitions);
}

