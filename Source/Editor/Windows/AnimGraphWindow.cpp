#include "AnimGraphWindow.h"
#include "Engine/GamePlay/Animation/Data/AnimGraphSerializer.h"
#include "Engine/Core/Math/StringHash.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

#include "Engine/Platform/Dialog.h"
#include "Engine/Platform/Logger.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <filesystem>

namespace ed = ax::NodeEditor;

const uint32_t ENTRY_NODE_ID = 0x0000000A; // 10
const uint32_t ENTRY_OUT_PIN = 0x0000000B; // 11
const int ENTRY_LINK_ID = 0x0000000C; // 12

AnimGraphWindow::AnimGraphWindow() : EditorWindow("Anim Graph Editor") {
    _nodeContext = ed::CreateEditor();
}

AnimGraphWindow::~AnimGraphWindow() {
    if (_nodeContext) ed::DestroyEditor(_nodeContext);
}

void AnimGraphWindow::DrawContents(EditorContext& context) {
    ed::SetCurrentEditor(_nodeContext);

    DrawToolbar();

    ImGui::Columns(2, "AnimGraphColumns");
    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.75f);

    DrawNodeWorkspace();

    ImGui::NextColumn();
    DrawInspector();
    ImGui::Columns(1);
}

void AnimGraphWindow::DrawNodeWorkspace()
{
    ed::Begin("My Anim Node Editor");

    // リンクの張りを0にして直線にする
    ed::PushStyleVar(ed::StyleVar_LinkStrength, 0.0f);

    constexpr float NODE_WIDTH = 130.0f;

    // ================================================================
    // STEP 1: ノードとピンの描画 (変更なし)
    // ================================================================
    ed::PushStyleColor(ed::StyleColor_NodeBg, ImVec4(0.42f, 0.26f, 0.00f, 1.0f));
    ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(1.00f, 0.60f, 0.00f, 1.0f));
    ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(12, 8, 12, 8));

    ed::BeginNode(ENTRY_NODE_ID);
    ImGui::Dummy(ImVec2(NODE_WIDTH, 0));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (NODE_WIDTH - ImGui::CalcTextSize("Entry").x) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Entry");

    ImGui::SameLine(NODE_WIDTH - ImGui::CalcTextSize(">").x);
    ed::PushStyleVar(ed::StyleVar_PinRadius, 0.0f);
    ed::BeginPin(ENTRY_OUT_PIN, ed::PinKind::Output);
    ImGui::TextDisabled(">");
    ed::EndPin();
    ed::PopStyleVar();
    ed::EndNode();
    ed::PopStyleVar(2);
    ed::PopStyleColor(2);

    for (auto& state : _currentGraph.states)
    {
        const bool isEntry = (_currentGraph.entryStateHash == state.stateHash);

        ed::PushStyleColor(ed::StyleColor_NodeBg, isEntry ? ImVec4(0.10f, 0.28f, 0.10f, 1.0f) : ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, isEntry ? ImVec4(0.30f, 0.75f, 0.30f, 1.0f) : ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(12, 8, 12, 8));

        ed::BeginNode(state.stateHash);

        ed::BeginPin(state.stateHash + 1, ed::PinKind::Input);
        ImGui::Dummy(ImVec2(NODE_WIDTH - 20.0f, 0));
        float tw = ImGui::CalcTextSize(state.sequenceName.c_str()).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fmaxf(0.0f, (NODE_WIDTH - 20.0f - tw) * 0.5f));
        ImGui::TextUnformatted(state.sequenceName.c_str());
        ed::EndPin();

        ImGui::SameLine(NODE_WIDTH - ImGui::CalcTextSize(">").x);

        ed::PushStyleVar(ed::StyleVar_PinRadius, 0.0f);
        ed::BeginPin(state.stateHash + 2, ed::PinKind::Output);
        ImGui::TextDisabled(">");
        ed::EndPin();
        ed::PopStyleVar();

        ed::EndNode();
        ed::PopStyleVar(2);
        ed::PopStyleColor(2);
    }

    if (_nodeToPlaceHash != 0) {
        ImVec2 c = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
        ed::SetNodePosition(_nodeToPlaceHash, ed::ScreenToCanvas(ImVec2(c.x + s.x * 0.5f, c.y + s.y * 0.5f)));
        ed::SelectNode(_nodeToPlaceHash);
        ed::NavigateToSelection(false, 0.5f);
        _nodeToPlaceHash = 0;
    }

    // ================================================================
    // STEP 2: 論理リンクの登録 (完全に透明にして描画させない)
    // ================================================================
    // imgui-node-editor には「接続されている」という事実だけを教え、描画はサボらせる
    const ImVec4 INVISIBLE_COLOR = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    if (_currentGraph.entryStateHash != 0) {
        ed::Link(ENTRY_LINK_ID, ENTRY_OUT_PIN, _currentGraph.entryStateHash + 1, INVISIBLE_COLOR, 0.0f);
    }

    int linkId = 0x00000100;
    for (const auto& state : _currentGraph.states) {
        for (const auto& trans : state.transitions) {
            ed::Link(linkId++, state.stateHash + 2, trans.targetStateHash + 1, INVISIBLE_COLOR, 0.0f);
        }
    }
    ed::PopStyleVar();

    // ================================================================
    // STEP 3: カスタム描画 (外周から外周への直線と矢印)
    // ================================================================
    ed::Suspend();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        auto drawCustomTransition = [&](uint32_t srcId, uint32_t dstId, ImU32 col) {
            ImVec2 sP = ed::GetNodePosition(srcId), sS = ed::GetNodeSize(srcId);
            ImVec2 dP = ed::GetNodePosition(dstId), dS = ed::GetNodeSize(dstId);

            // ノードの厳密な中心座標
            ImVec2 sCenter = ImVec2(sP.x + sS.x * 0.5f, sP.y + sS.y * 0.5f);
            ImVec2 dCenter = ImVec2(dP.x + dS.x * 0.5f, dP.y + dS.y * 0.5f);

            // 双方向（A->B と B->A）の遷移が存在するかチェック
            bool isBidirectional = false;
            if (srcId != ENTRY_NODE_ID && dstId != ENTRY_NODE_ID) {
                for (const auto& st : _currentGraph.states) {
                    if (st.stateHash == dstId) {
                        for (const auto& tr : st.transitions) {
                            if (tr.targetStateHash == srcId) isBidirectional = true;
                        }
                    }
                }
            }

            // 方向ベクトルの計算
            ImVec2 dir = ImVec2(dCenter.x - sCenter.x, dCenter.y - sCenter.y);
            float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
            if (len < 1.0f) return;
            dir.x /= len; dir.y /= len;

            // ★ハック: 双方向の場合は、線を進行方向の右側に少しズラす（重なり回避）
            if (isBidirectional) {
                ImVec2 perp = ImVec2(-dir.y, dir.x);
                float offsetAmt = 8.0f; // ズラす幅
                sCenter.x += perp.x * offsetAmt; sCenter.y += perp.y * offsetAmt;
                dCenter.x += perp.x * offsetAmt; dCenter.y += perp.y * offsetAmt;
            }

            // 中心から AABB(矩形) の外周（Perimeter）までの距離を計算するヘルパー
            auto getPerimeterPoint = [](ImVec2 c, ImVec2 s, ImVec2 d) -> ImVec2 {
                float hw = s.x * 0.5f;
                float hh = s.y * 0.5f;
                float t = 1e9f;
                // X軸とY軸の境界のうち、近い方を交点とする
                if (fabsf(d.x) > 1e-4f) t = fminf(t, hw / fabsf(d.x));
                if (fabsf(d.y) > 1e-4f) t = fminf(t, hh / fabsf(d.y));
                return ImVec2(c.x + d.x * t, c.y + d.y * t);
                };

            // 線の開始地点と終了地点を「ノードの外周」に設定
            ImVec2 pStart = getPerimeterPoint(sCenter, sS, dir);
            // ターゲット側は逆方向のレイを飛ばして外周を求める
            ImVec2 pEnd = getPerimeterPoint(dCenter, dS, ImVec2(-dir.x, -dir.y));

            // キャンバス座標からスクリーン座標へ変換
            ImVec2 screenStart = ed::CanvasToScreen(pStart);
            ImVec2 screenEnd = ed::CanvasToScreen(pEnd);

            // 1. 直線の描画
            dl->AddLine(screenStart, screenEnd, col, 2.5f);

            // 2. 矢印の描画 (終点に配置)
            constexpr float A = 14.0f; // 矢印のサイズ
            ImVec2 tip = screenEnd;
            ImVec2 perp = ImVec2(-dir.y, dir.x);
            dl->AddTriangleFilled(
                tip,
                ImVec2(tip.x - dir.x * A + perp.x * A * 0.5f, tip.y - dir.y * A + perp.y * A * 0.5f),
                ImVec2(tip.x - dir.x * A - perp.x * A * 0.5f, tip.y - dir.y * A - perp.y * A * 0.5f),
                col);
            };

        // 描画の実行
        if (_currentGraph.entryStateHash != 0) {
            drawCustomTransition(ENTRY_NODE_ID, _currentGraph.entryStateHash, IM_COL32(255, 153, 0, 255));
        }

        for (const auto& state : _currentGraph.states) {
            for (const auto& trans : state.transitions) {
                // 選択されているリンクなら色を明るくする等の処理もここで可能
                drawCustomTransition(state.stateHash, trans.targetStateHash, IM_COL32(200, 200, 200, 255));
            }
        }
    }
    ed::Resume();

    // ================================================================
    // STEP 4: インタラクション (変更なし)
    // ================================================================
    HandleInteraction();

    ed::End();

    ed::NodeId selNode; ed::LinkId selLink;
    _selectedNodeHash = 0; _selectedLinkId = -1;
    if (ed::GetSelectedNodes(&selNode, 1) > 0)
        _selectedNodeHash = (uint32_t)(uintptr_t)selNode.Get();
    else if (ed::GetSelectedLinks(&selLink, 1) > 0)
        _selectedLinkId = (int)(uintptr_t)selLink.Get();
}
void AnimGraphWindow::HandleInteraction()
{
    // ★ 完璧な配線ロジック (逆引き・不正判定に対応)
    if (ed::BeginCreate()) {
        ed::PinId startPinId, endPinId;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            if (startPinId && endPinId) {
                uint32_t startId = (uint32_t)(uintptr_t)startPinId.Get();
                uint32_t endId = (uint32_t)(uintptr_t)endPinId.Get();

                bool isStartInput = false;
                bool isEndInput = false;
                uint32_t srcHash = 0;
                uint32_t dstHash = 0;

                if (startId == ENTRY_OUT_PIN) {
                    isStartInput = false; srcHash = ENTRY_NODE_ID;
                }
                else {
                    for (auto& s : _currentGraph.states) {
                        if (startId == s.stateHash + 1) { isStartInput = true; srcHash = s.stateHash; break; }
                        if (startId == s.stateHash + 2) { isStartInput = false; srcHash = s.stateHash; break; }
                    }
                }

                if (endId == ENTRY_OUT_PIN) {
                    isEndInput = false; dstHash = ENTRY_NODE_ID;
                }
                else {
                    for (auto& s : _currentGraph.states) {
                        if (endId == s.stateHash + 1) { isEndInput = true; dstHash = s.stateHash; break; }
                        if (endId == s.stateHash + 2) { isEndInput = false; dstHash = s.stateHash; break; }
                    }
                }

                uint32_t finalSrc = isStartInput ? dstHash : srcHash;
                uint32_t finalDst = isStartInput ? srcHash : dstHash;

                if (isStartInput == isEndInput || finalDst == ENTRY_NODE_ID) {
                    ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                }
                else {
                    if (ed::AcceptNewItem(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), 2.0f)) {
                        if (finalSrc == ENTRY_NODE_ID) {
                            _currentGraph.entryStateHash = finalDst;
                        }
                        else {
                            for (auto& s : _currentGraph.states) {
                                if (s.stateHash == finalSrc) {
                                    // 重複接続の防止
                                    bool exists = false;
                                    for (auto& t : s.transitions) {
                                        if (t.targetStateHash == finalDst) { exists = true; break; }
                                    }
                                    if (!exists) {
                                        AnimTransition t;
                                        t.targetStateHash = finalDst;
                                        s.transitions.push_back(t);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        ed::PinId unusedPin;
        if (ed::QueryNewNode(&unusedPin)) ed::RejectNewItem();
    }
    ed::EndCreate();

    // 削除処理
    if (ed::BeginDelete()) {
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                int id = (int)(uintptr_t)linkId.Get();
                if (id == ENTRY_LINK_ID) {
                    _currentGraph.entryStateHash = 0;
                }
                else {
                    int currentId = 0x00000100;
                    bool deleted = false;
                    for (auto& s : _currentGraph.states) {
                        for (auto it = s.transitions.begin(); it != s.transitions.end(); ++it) {
                            if (currentId == id) {
                                s.transitions.erase(it);
                                deleted = true;
                                break;
                            }
                            currentId++;
                        }
                        if (deleted) break;
                    }
                }
            }
        }

        ed::NodeId nodeId;
        while (ed::QueryDeletedNode(&nodeId)) {
            uint32_t targetHash = (uint32_t)(uintptr_t)nodeId.Get();

            if (targetHash == ENTRY_NODE_ID) {
                ed::RejectDeletedItem();
                continue;
            }

            if (ed::AcceptDeletedItem()) {
                if (_currentGraph.entryStateHash == targetHash) _currentGraph.entryStateHash = 0;
                _currentGraph.states.erase(
                    std::remove_if(_currentGraph.states.begin(), _currentGraph.states.end(),
                        [targetHash](const AnimState& s) { return s.stateHash == targetHash; }),
                    _currentGraph.states.end());
            }
        }
    }
    ed::EndDelete();

    // 右クリックメニュー
    ed::Suspend();
    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("CanvasContextMenu");
    }
    if (ImGui::BeginPopup("CanvasContextMenu")) {
        ImGui::Text("キャンバス操作 (Canvas Actions)");
        ImGui::Separator();
        if (ImGui::MenuItem("新しいステートを追加 (Add State)")) {
            std::string name = "NewState_" + std::to_string(_currentGraph.states.size());
            AnimState newState;
            newState.stateHash = CCL::Utils::HashString(name.c_str());
            newState.sequenceName = name;
            _currentGraph.states.push_back(newState);
            _nodeToPlaceHash = newState.stateHash;
        }
        ImGui::EndPopup();
    }
    ed::Resume();
}

void AnimGraphWindow::DrawInspector() {
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    if (_selectedNodeHash != 0) {
        DrawStateSettings(_selectedNodeHash);
    }
    else if (_selectedLinkId != -1) {
        DrawTransitionSettings(_selectedLinkId);
    }
    else {
        ImGui::TextDisabled("項目を選択してください。\n(Select a Node or Link)");
    }
}

void AnimGraphWindow::DrawStateSettings(uint32_t stateHash) {
    if (stateHash == ENTRY_NODE_ID) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "開始地点 (Entry Node)");
        ImGui::Separator();
        ImGui::TextDisabled("ここから最初に再生したいステートへ\nワイヤーを引いて接続してください。");
        return;
    }

    for (auto& state : _currentGraph.states) {
        if (state.stateHash == stateHash) {
            char nameBuf[64];
            strcpy_s(nameBuf, state.sequenceName.c_str());
            if (ImGui::InputText("状態名 (State Name)", nameBuf, sizeof(nameBuf))) {
                state.sequenceName = nameBuf;
            }

            ImGui::Checkbox("ループ再生 (Loop Animation)", &state.isLoop);
            ImGui::DragFloat("再生速度 (Playback Speed)", &state.playbackSpeed, 0.05f, 0.0f, 5.0f);

            char curveBuf[128];
            strcpy_s(curveBuf, state.speedCurveName.c_str());
            if (ImGui::InputText("速度カーブ (Speed Curve)", curveBuf, sizeof(curveBuf))) {
                state.speedCurveName = curveBuf;
            }

            ImGui::Separator();
            if (_currentGraph.entryStateHash == state.stateHash) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[ 初期状態 (Entry State) ]");
                ImGui::TextDisabled("※このノードが最初に再生されます。");
            }
            break;
        }
    }
}

void AnimGraphWindow::DrawTransitionSettings(int targetLinkId) {
    if (targetLinkId == ENTRY_LINK_ID) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "初期状態への遷移 (Entry Transition)");
        ImGui::Separator();
        ImGui::TextDisabled("※これはゲーム開始時に無条件で遷移するため、\n条件(Condition)を設定することはできません。");
        return;
    }

    int currentId = 0x00000100;
    for (auto& state : _currentGraph.states) {
        for (auto& trans : state.transitions) {
            if (currentId == targetLinkId) {

                std::string targetName = "Unknown";
                for (const auto& s : _currentGraph.states) {
                    if (s.stateHash == trans.targetStateHash) targetName = s.sequenceName;
                }

                ImGui::Text("遷移元 (From): %s", state.sequenceName.c_str());
                ImGui::Text("遷移先 (To): %s", targetName.c_str());
                ImGui::Separator();

                ImGui::Text("遷移条件 (Conditions)");
                if (ImGui::Button("+ 条件を追加 (Add Condition)")) {
                    trans.conditions.push_back({
                        CCL::Utils::HashString("Speed"),
                        "Speed",
                        AnimConditionOp::Greater,
                        0.0f
                        });
                }

                for (size_t c = 0; c < trans.conditions.size(); ++c) {
                    auto& cond = trans.conditions[c];
                    ImGui::PushID((int)c);

                    ImGui::Separator();
                    ImGui::Text("条件 (Condition) %d", (int)c);

                    ImGui::Text("対象パラメータ (Parameter Name):");

                    char paramBuf[64];
                    strcpy_s(paramBuf, cond.paramName.c_str());
                    if (ImGui::InputText("##ParamName", paramBuf, sizeof(paramBuf))) {
                        cond.paramName = paramBuf;
                        cond.paramHash = CCL::Utils::HashString(paramBuf);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("パラメータ名を入力してください。ハッシュ値は自動計算されます。");
                    }
                    ImGui::TextDisabled("ID: 0x%08X", cond.paramHash);

                    int op = (int)cond.op;
                    ImGui::SetNextItemWidth(150);
                    const char* ops[] = { "一致 (Equal)", "より大きい (Greater)", "より小さい (Less)" };
                    if (ImGui::Combo("条件 (Op)", &op, ops, IM_ARRAYSIZE(ops))) {
                        cond.op = (AnimConditionOp)op;
                    }

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    ImGui::DragFloat("閾値 (Value)", &cond.threshold, 0.05f);

                    if (ImGui::Button("削除 (Delete)")) {
                        trans.conditions.erase(trans.conditions.begin() + c);
                        ImGui::PopID();
                        break;
                    }

                    ImGui::PopID();
                }
                return;
            }
            currentId++;
        }
    }
}

// ====================================================================
// ツールバー描画 
// ====================================================================
void AnimGraphWindow::DrawToolbar() {
    if (ImGui::Button("保存 (Save As...)")) {
        char filename[MAX_PATH] = {};
        auto result = Dialog::SaveFileName(
            filename, MAX_PATH, "JSON Files\0*.json\0", "Save Anim Graph", "json", Graphics::Instance().GetWindowHandle()
        );

        if (result == DialogResult::OK) {
            namespace fs = std::filesystem;
            fs::path absPath = filename;
            fs::path currentPath = fs::current_path();
            std::error_code ec;
            fs::path relPath = fs::relative(absPath, currentPath, ec);
            _currentFilePath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;
            SaveGraph(_currentFilePath);
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("読込 (Load...)")) {
        char filename[MAX_PATH] = {};
        auto result = Dialog::OpenFileName(
            filename, MAX_PATH, "JSON Files\0*.json\0", "Select Anim Graph File", Graphics::Instance().GetWindowHandle()
        );

        if (result == DialogResult::OK) {
            namespace fs = std::filesystem;
            fs::path absPath = filename;
            fs::path currentPath = fs::current_path();
            std::error_code ec;
            fs::path relPath = fs::relative(absPath, currentPath, ec);
            _currentFilePath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;
            LoadGraph(_currentFilePath);
        }
    }

    ImGui::SameLine();
    ImGui::TextDisabled("File: %s", _currentFilePath.empty() ? "None" : _currentFilePath.c_str());

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    if (ImGui::Button("全表示 (Focus All)")) ed::NavigateToContent();

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::SetNextItemWidth(120);
    ImGui::InputText("##NewStateName", _newStateName, sizeof(_newStateName));
    ImGui::SameLine();
    if (ImGui::Button("追加 (+ Add State)") && _newStateName[0] != '\0') {
        AnimState newState;
        newState.stateHash = CCL::Utils::HashString(_newStateName);
        newState.sequenceName = _newStateName;
        _currentGraph.states.push_back(newState);
        _nodeToPlaceHash = newState.stateHash;
        _newStateName[0] = '\0';
    }
}

void AnimGraphWindow::LoadGraph(const std::string& path) {
    if (AnimGraphSerializer::LoadFromJSON(_currentGraph, path, {})) {
        ed::ClearSelection();
        ed::NavigateToContent();
    }
}

void AnimGraphWindow::SaveGraph(const std::string& path) {
    AnimGraphSerializer::SaveToJSON(_currentGraph, path);
}