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
using Engine::FileDialog;

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
// ★追加: STEP 1 - 矢印を描画するための特製ヘルパー関数
// (AnimGraphWindow::DrawContents などの関数よりも「上」に書きます)
// =====================================================================
static void DrawLinkArrow(ImDrawList* drawList, const ImVec2& p0, const ImVec2& p3, ImU32 color) {
    if (p0.x == 0.0f && p0.y == 0.0f) return;
    if (p3.x == 0.0f && p3.y == 0.0f) return;

    // NodeEditorの標準的な水平リンク(左から右)のベジェ制御点計算
    float strength = std::abs(p3.x - p0.x) * 0.5f;
    strength = std::fmax(strength, 50.0f); // 最低でも50pxは曲げる

    ImVec2 p1 = ImVec2(p0.x + strength, p0.y);
    ImVec2 p2 = ImVec2(p3.x - strength, p3.y);

    // 曲線の中間地点 (t = 0.5) を計算
    float t = 0.5f, u = 0.5f; // t=0.5なのでuも0.5
    float w1 = u * u * u, w2 = 3 * u * u * t, w3 = 3 * u * t * t, w4 = t * t * t;

    ImVec2 midPoint = ImVec2(
        w1 * p0.x + w2 * p1.x + w3 * p2.x + w4 * p3.x,
        w1 * p0.y + w2 * p1.y + w3 * p2.y + w4 * p3.y
    );

    // 中間地点での接線ベクトル（曲線の傾き＝矢印の向き）を計算
    ImVec2 tangent = ImVec2(
        -3 * u * u * p0.x + (3 * u * u - 6 * u * t) * p1.x + (6 * u * t - 3 * t * t) * p2.x + 3 * t * t * p3.x,
        -3 * u * u * p0.y + (3 * u * u - 6 * u * t) * p1.y + (6 * u * t - 3 * t * t) * p2.y + 3 * t * t * p3.y
    );

    float length = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
    if (length > 0.0001f) { tangent.x /= length; tangent.y /= length; }
    else { tangent = ImVec2(1, 0); }

    // 三角形の3頂点を計算
    float arrowSize = 10.0f; // 矢印の大きさ
    ImVec2 perp = ImVec2(-tangent.y, tangent.x); // 垂直ベクトル
    ImVec2 pt1 = ImVec2(midPoint.x + tangent.x * arrowSize, midPoint.y + tangent.y * arrowSize);
    ImVec2 pt2 = ImVec2(midPoint.x - tangent.x * arrowSize + perp.x * arrowSize * 0.7f, midPoint.y - tangent.y * arrowSize + perp.y * arrowSize * 0.7f);
    ImVec2 pt3 = ImVec2(midPoint.x - tangent.x * arrowSize - perp.x * arrowSize * 0.7f, midPoint.y - tangent.y * arrowSize - perp.y * arrowSize * 0.7f);

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
    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);

    constexpr float NODE_WIDTH = 150.0f;

    // ピン中央座標を記憶（キーは int のピンID）
    std::unordered_map<int, ImVec2> pinPositions;

    // ================================================================
    // 1. Entry ノード
    // ================================================================
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.3f, 0.2f, 0.1f, 1.0f));
    ed::BeginNode(ENTRY_NODE_ID);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 5));
    float ew = ImGui::CalcTextSize("ENTRY").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - ew) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "ENTRY");
    ImGui::Dummy(ImVec2(NODE_WIDTH, 5));
    ImGui::Separator();

    ImVec2 cursorPosE = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(cursorPosE, ImVec2(cursorPosE.x + NODE_WIDTH, cursorPosE.y + 24), IM_COL32(204, 102, 25, 200), 4.0f);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
    float twE = ImGui::CalcTextSize("● 線を伸ばす (Start)").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - twE) * 0.5f);

    ed::BeginPin(ENTRY_OUT_PIN, ed::PinKind::Output);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "● 線を伸ばす (Start)");
    ed::EndPin();

    pinPositions[ENTRY_OUT_PIN] = ImVec2(
        ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f,
        ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
    ed::EndNode();
    ed::PopStyleColor();

    if (_needRestorePositions)
        ed::SetNodePosition(ENTRY_NODE_ID, ImVec2(_currentGraph.entryPosX, _currentGraph.entryPosY));

    // ================================================================
    // 1.5. ANY STATE ノード
    // ================================================================
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.1f, 0.3f, 0.4f, 1.0f));
    ed::BeginNode(ANY_STATE_NODE_ID);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 5));
    float anyW = ImGui::CalcTextSize("ANY STATE").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - anyW) * 0.5f);
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "ANY STATE");
    ImGui::Dummy(ImVec2(NODE_WIDTH, 5));
    ImGui::Separator();

    ImVec2 cursorPosAny = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(cursorPosAny, ImVec2(cursorPosAny.x + NODE_WIDTH, cursorPosAny.y + 24), IM_COL32(204, 102, 25, 200), 4.0f);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
    float twAny = ImGui::CalcTextSize("● 線を伸ばす (Out)").x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - twAny) * 0.5f);

    ed::BeginPin(ANY_STATE_OUT_PIN, ed::PinKind::Output);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "● 線を伸ばす (Out)");
    ed::EndPin();

    pinPositions[ANY_STATE_OUT_PIN] = ImVec2(
        ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f,
        ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f);

    ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
    ed::EndNode();
    ed::PopStyleColor();

    if (_needRestorePositions)
        ed::SetNodePosition(ANY_STATE_NODE_ID, ImVec2(_currentGraph.anyStatePosX, _currentGraph.anyStatePosY));

    // ================================================================
    // 2. 通常ステートノード
    // ================================================================
    for (auto &state : _currentGraph.states)
    {
        ImGui::PushID(state.stateHash);

        const bool isEntry  = (_currentGraph.entryStateHash == state.stateHash);
        const bool isActive = (_runtimeActiveStateHash == state.stateHash);

        ImVec4 nodeBgColor = isEntry ? ImVec4(0.15f, 0.3f, 0.15f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        if (isActive)
            nodeBgColor = ImVec4(0.1f, 0.4f, 0.6f, 1.0f);
        ed::PushStyleColor(ed::StyleColor_NodeBg, nodeBgColor);

        if (isActive)
        {
            float  pulse       = (sinf((float)ImGui::GetTime() * 8.0f) + 1.0f) * 0.5f;
            ImVec4 borderColor = ImVec4(0.2f + pulse * 0.4f, 0.7f + pulse * 0.3f, 1.0f, 1.0f);
            ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 2.5f + pulse * 2.0f);
        }

        ed::BeginNode(StateNodeId(state.stateHash));

        // 入力ピン層
        ImVec2 cursorPosI = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPosI, ImVec2(cursorPosI.x + NODE_WIDTH, cursorPosI.y + 24), IM_COL32(51, 153, 51, 200), 4.0f);

        ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        float twI = ImGui::CalcTextSize("▼ ドロップで繋ぐ (In)").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - twI) * 0.5f);

        ed::BeginPin(StateInPin(state.stateHash), ed::PinKind::Input);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "▼ ドロップで繋ぐ (In)");
        ed::EndPin();

        pinPositions[StateInPin(state.stateHash)] = ImVec2(
            ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f,
            ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f);

        ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        ImGui::Separator();

        // 中央層
        ImGui::Dummy(ImVec2(NODE_WIDTH, 8));
        std::string displayText = state.sequenceName;
        if (isActive)
            displayText = "▶ " + displayText;
        float tw = ImGui::CalcTextSize(displayText.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - tw) * 0.5f);
        if (isActive)
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", displayText.c_str());
        else
            ImGui::TextUnformatted(displayText.c_str());
        ImGui::Dummy(ImVec2(NODE_WIDTH, 8));
        ImGui::Separator();

        // 出力ピン層
        ImVec2 cursorPosO = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(cursorPosO, ImVec2(cursorPosO.x + NODE_WIDTH, cursorPosO.y + 24), IM_COL32(204, 102, 25, 200), 4.0f);

        ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        float twO = ImGui::CalcTextSize("● 線を伸ばす (Out)").x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - twO) * 0.5f);

        ed::BeginPin(StateOutPin(state.stateHash), ed::PinKind::Output);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "● 線を伸ばす (Out)");
        ed::EndPin();

        pinPositions[StateOutPin(state.stateHash)] = ImVec2(
            ImGui::GetItemRectMin().x + ImGui::GetItemRectSize().x * 0.5f,
            ImGui::GetItemRectMin().y + ImGui::GetItemRectSize().y * 0.5f);

        ImGui::Dummy(ImVec2(NODE_WIDTH, 4));
        ed::EndNode();

        if (isActive)
        {
            ed::PopStyleVar();
            ed::PopStyleColor();
        }
        ed::PopStyleColor();
        ImGui::PopID();

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

    // ================================================================
    // 3. リンク描画 ＋ _linkLut 再構築
    // ================================================================
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    _linkLut.clear();

    // Entry リンク
    if (_currentGraph.entryStateHash != 0)
    {
        ed::Link(ENTRY_LINK_ID, ENTRY_OUT_PIN, StateInPin(_currentGraph.entryStateHash), ImVec4(1.0f, 0.6f, 0.0f, 1.0f), 2.5f);
        if (pinPositions.count(ENTRY_OUT_PIN) && pinPositions.count(StateInPin(_currentGraph.entryStateHash)))
            DrawLinkArrow(drawList, pinPositions[ENTRY_OUT_PIN], pinPositions[StateInPin(_currentGraph.entryStateHash)], IM_COL32(255, 153, 0, 255));
    }

    // 通常遷移
    int linkId = NORMAL_LINK_BASE;
    for (const auto &state : _currentGraph.states)
    {
        for (const auto &trans : state.transitions)
        {
            int srcPin = StateOutPin(state.stateHash);
            int dstPin = StateInPin(trans.targetStateHash);
            ed::Link(linkId, srcPin, dstPin, ImVec4(0.8f, 0.8f, 0.8f, 1.0f), 2.5f);
            _linkLut.push_back({linkId, LinkRef{state.stateHash, trans.targetStateHash, false}});
            ++linkId;

            if (pinPositions.count(srcPin) && pinPositions.count(dstPin))
            {
                ImU32 arrowColor = (_runtimeActiveStateHash == state.stateHash) ? IM_COL32(100, 200, 255, 255) : IM_COL32(200, 200, 200, 255);
                DrawLinkArrow(drawList, pinPositions[srcPin], pinPositions[dstPin], arrowColor);
            }
        }
    }

    // Any State 遷移
    int anyLinkId = ANY_LINK_BASE;
    for (const auto &trans : _currentGraph.anyStateTransitions)
    {
        int dstPin = StateInPin(trans.targetStateHash);
        ed::Link(anyLinkId, ANY_STATE_OUT_PIN, dstPin, ImVec4(0.4f, 0.8f, 0.8f, 1.0f), 2.5f);
        _linkLut.push_back({anyLinkId, LinkRef{0, trans.targetStateHash, true}});
        ++anyLinkId;

        if (pinPositions.count(ANY_STATE_OUT_PIN) && pinPositions.count(dstPin))
            DrawLinkArrow(drawList, pinPositions[ANY_STATE_OUT_PIN], pinPositions[dstPin], IM_COL32(100, 200, 200, 255));
    }

    ed::PopStyleVar();

    if (_needRestorePositions)
    {
        ed::NavigateToContent();
        _needRestorePositions = false;
    }

    HandleInteraction();
    HandleContextMenu();

    ed::End();

    // ================================================================
    // 選択状態の取得（0 = 未選択）
    // ================================================================
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
    // ピンの素性（入力か／所属ノードID／所属ステートハッシュ）を特定する
    auto getPinInfo = [&](int pinId, bool &isInput, uint32_t &nodeHash, int &nodeId) -> bool {
        if (pinId == ENTRY_OUT_PIN)
        {
            isInput  = false;
            nodeHash = 0;
            nodeId   = ENTRY_NODE_ID;
            return true;
        }
        if (pinId == ANY_STATE_OUT_PIN)
        {
            isInput  = false;
            nodeHash = 0;
            nodeId   = ANY_STATE_NODE_ID;
            return true;
        }
        for (auto &s : _currentGraph.states)
        {
            if (pinId == StateInPin(s.stateHash))
            {
                isInput  = true;
                nodeHash = s.stateHash;
                nodeId   = StateNodeId(s.stateHash);
                return true;
            }
            if (pinId == StateOutPin(s.stateHash))
            {
                isInput  = false;
                nodeHash = s.stateHash;
                nodeId   = StateNodeId(s.stateHash);
                return true;
            }
        }
        return false;
    };

    // =============================================================
    // 1. リンク作成
    // =============================================================
    if (ed::BeginCreate())
    {
        ed::PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId))
        {
            if (startPinId && endPinId)
            {
                int startId = (int)(intptr_t)startPinId.Get();
                int endId   = (int)(intptr_t)endPinId.Get();

                bool     isStartInput = false, isEndInput = false;
                uint32_t srcHash = 0, dstHash = 0;
                int      srcNode = 0, dstNode = 0;

                if (getPinInfo(startId, isStartInput, srcHash, srcNode) &&
                    getPinInfo(endId, isEndInput, dstHash, dstNode))
                {

                    if (isStartInput == isEndInput)
                    {
                        ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                    }
                    else
                    {
                        // 逆引きドラッグ対策：出発(Src)と目的(Dst)を確定
                        uint32_t finalSrcHash = isStartInput ? dstHash : srcHash;
                        uint32_t finalDstHash = isStartInput ? srcHash : dstHash;
                        int      finalSrcNode = isStartInput ? dstNode : srcNode;
                        int      finalDstNode = isStartInput ? srcNode : dstNode;

                        // Entry/AnyState は目的地になれない
                        if (finalDstNode == ENTRY_NODE_ID || finalDstNode == ANY_STATE_NODE_ID)
                        {
                            ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                        }
                        else if (ed::AcceptNewItem(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f))
                        {

                            if (finalSrcNode == ENTRY_NODE_ID)
                            {
                                _currentGraph.entryStateHash = finalDstHash;
                            }
                            else if (finalSrcNode == ANY_STATE_NODE_ID)
                            {
                                bool exists = false;
                                for (auto &t : _currentGraph.anyStateTransitions)
                                    if (t.targetStateHash == finalDstHash)
                                    {
                                        exists = true;
                                        break;
                                    }
                                if (!exists)
                                {
                                    AnimTransition t;
                                    t.targetStateHash = finalDstHash;
                                    _currentGraph.anyStateTransitions.push_back(t);
                                }
                            }
                            else
                            {
                                for (auto &s : _currentGraph.states)
                                {
                                    if (s.stateHash == finalSrcHash)
                                    {
                                        bool exists = false;
                                        for (auto &t : s.transitions)
                                            if (t.targetStateHash == finalDstHash)
                                            {
                                                exists = true;
                                                break;
                                            }
                                        if (!exists)
                                        {
                                            AnimTransition t;
                                            t.targetStateHash = finalDstHash;
                                            s.transitions.push_back(t);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                }
            }
        }
        ed::PinId unusedPin;
        if (ed::QueryNewNode(&unusedPin))
            ed::RejectNewItem();
    }
    ed::EndCreate();

    // =============================================================
    // 2. 削除（_linkLut で素性解決。位置数え直しを廃止）
    // =============================================================
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
                // ノードIDからステートハッシュを逆引き（オーバーフロー安全に走査で）
                uint32_t targetHash = 0;
                bool     found      = false;
                for (auto &s : _currentGraph.states)
                    if (StateNodeId(s.stateHash) == rawId)
                    {
                        targetHash = s.stateHash;
                        found      = true;
                        break;
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
        if (auto path = FileDialog::SavePreset("AnimNodes", "Save Anim Graph"))
        {
            _currentFilePath = FileDialog::ConvertToRelativePath(*path);
            SaveGraph(_currentFilePath);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("読込 (Load...)"))
    {
        if (auto path = FileDialog::OpenPreset("AnimNodes", "Select Anim Graph File"))
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
}

void AnimGraphWindow::LoadGraph(const std::string& path) {
    if (AnimGraphSerializer::LoadFromJSON(_currentGraph, path, {})) {
        ed::ClearSelection();
        // ★ ロードが成功したら、次のフレームで座標を復元するフラグを立てる
        _needRestorePositions = true;
    }
}

void AnimGraphWindow::HandleContextMenu() {
    ed::Suspend();

    // キャンバスの背景で右クリックされたか判定
    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("AnimCanvasContextMenu");
        // ★右クリックした瞬間のマウス座標（スクリーン座標）を記憶
        _newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("AnimCanvasContextMenu")) {
        ImGui::Text("キャンバス操作 (Canvas Actions)");
        ImGui::Separator();

        // 状態名を入力させる
        ImGui::SetNextItemWidth(150);
        ImGui::InputText("##NewStateNameCtx", _newStateName, sizeof(_newStateName));
        ImGui::SameLine();

        // 追加ボタン
        if (ImGui::Button("新しいステートを追加") && _newStateName[0] != '\0') {

            AnimState newState;
            newState.stateHash = CCL::Utils::HashString(_newStateName);
            newState.sequenceName = _newStateName;

            // ★記憶しておいたスクリーン座標を、広大なキャンバスの仮想座標（Canvas）に変換する！
            ImVec2 canvasPos = ed::ScreenToCanvas(_newNodeSpawnPos);

            // ノードの初期座標を設定（Bake時などに使われる構造体の値）
            newState.nodePosX = canvasPos.x;
            newState.nodePosY = canvasPos.y;

            _currentGraph.states.push_back(newState);

            // ★生成した瞬間にその場所にノードを配置するように指示
            _nodeToPlaceHash = newState.stateHash;

            // 入力欄をクリア
            _newStateName[0] = '\0';

            // メニューを閉じる
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

    for (size_t c = 0; c < trans.conditions.size(); ++c)
    {
        auto &cond = trans.conditions[c];
        ImGui::PushID((int)c);
        ImGui::Separator();
        ImGui::Text("条件 (Condition) %d", (int)c);
        ImGui::Text("対象パラメータ (Parameter):");
        ImGui::SetNextItemWidth(-1);

        // 登録済みパラメータからドロップダウンで選択（手入力＋ハッシュ手計算を廃止）
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

        if (ImGui::Button("削除 (Delete)"))
        {
            trans.conditions.erase(trans.conditions.begin() + c);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
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

