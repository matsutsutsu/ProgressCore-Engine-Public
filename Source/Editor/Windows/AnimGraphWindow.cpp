#include "AnimGraphWindow.h"
#include "Engine/GamePlay/Animation/Data/AnimGraphSerializer.h"
#include "Engine/Core/Math/StringHash.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace ed = ax::NodeEditor;

// ====================================================================
// ★ プロ仕様のID管理アーキテクチャ (ID衝突の完全防止)
// ====================================================================
// ノードやピンのIDと、リンクのIDが被らないように空間を完全に分離する
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

void AnimGraphWindow::DrawNodeWorkspace() {
    ed::Begin("My Anim Node Editor");

    // ================================================================
    // STEP 1: 全ノードを先に描画する (ピンをすべて登録する)
    // ================================================================

    // Entry ノード
    ed::BeginNode(ENTRY_NODE_ID);
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "開始地点 (Entry)");
    ed::BeginPin(ENTRY_OUT_PIN, ed::PinKind::Output);
    ImGui::Text("開始 ->");
    ed::EndPin();
    ed::EndNode();

    // 通常ノード (全て描画してピンを登録する)
    for (auto& state : _currentGraph.states) {
        ed::BeginNode(state.stateHash);
        ImGui::Text("%s", state.sequenceName.c_str());

        ed::BeginPin(state.stateHash + 1, ed::PinKind::Input);
        ImGui::Text("-> 入力 (In)");
        ed::EndPin();

        ImGui::SameLine();

        ed::BeginPin(state.stateHash + 2, ed::PinKind::Output);
        ImGui::Text("出力 (Out) ->");
        ed::EndPin();

        ed::EndNode();
    }

    // ノード配置リクエストの処理 (ノード描画直後、リンク描画前)
    if (_nodeToPlaceHash != 0) {
        ImVec2 centerPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 spawnPos = ed::ScreenToCanvas(
            ImVec2(centerPos.x + windowSize.x * 0.5f,
                centerPos.y + windowSize.y * 0.5f));
        ed::SetNodePosition(_nodeToPlaceHash, spawnPos);
        ed::SelectNode(_nodeToPlaceHash);
        ed::NavigateToSelection(false, 0.5f);
        _nodeToPlaceHash = 0;
    }

    // ================================================================
    // STEP 2: 全ピンが登録された後に、全リンクを描画する
    // ================================================================

    // ★修正: Entry リンクをここで描画 (接続先ピンが登録済みなので正常動作)
    if (_currentGraph.entryStateHash != 0) {
        ed::PushStyleColor(ed::StyleColor_Flow, ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
        ed::Link(ENTRY_LINK_ID, ENTRY_OUT_PIN, _currentGraph.entryStateHash + 1);
        ed::PopStyleColor();
    }

    // 通常リンク
    int linkId = 0x00000100;
    for (const auto& state : _currentGraph.states) {
        for (const auto& trans : state.transitions) {
            ed::Link(linkId++, state.stateHash + 2, trans.targetStateHash + 1);
        }
    }

    // ================================================================
    // STEP 3: インタラクション処理
    // ================================================================
    HandleInteraction();

    ed::End();

    // 選択状態の取得 (ed::End の外)
    ed::NodeId selectedNodeId;
    ed::LinkId selectedLinkId;
    _selectedNodeHash = 0;
    _selectedLinkId = -1;

    if (ed::GetSelectedNodes(&selectedNodeId, 1) > 0) {
        _selectedNodeHash = (uint32_t)(uintptr_t)selectedNodeId.Get();
    }
    else if (ed::GetSelectedLinks(&selectedLinkId, 1) > 0) {
        _selectedLinkId = (int)(uintptr_t)selectedLinkId.Get();
    }
}

void AnimGraphWindow::HandleInteraction() {
    // ====================================================================
    // 新しいリンク作成ロジック (逆引き・不正接続の完全対応)
    // ====================================================================
    if (ed::BeginCreate()) {
        ed::PinId startPin, endPin;
        if (ed::QueryNewLink(&startPin, &endPin)) {
            if (startPin && endPin) {
                uint32_t startId = (uint32_t)(uintptr_t)startPin.Get();
                uint32_t endId = (uint32_t)(uintptr_t)endPin.Get();

                bool isStartInput = false;
                bool isEndInput = false;
                uint32_t srcHash = 0;
                uint32_t dstHash = 0;

                if (startId == ENTRY_OUT_PIN) {
                    isStartInput = false;
                    srcHash = ENTRY_NODE_ID;
                }
                else {
                    for (auto& s : _currentGraph.states) {
                        if (startId == s.stateHash + 1) { isStartInput = true; srcHash = s.stateHash; break; }
                        if (startId == s.stateHash + 2) { isStartInput = false; srcHash = s.stateHash; break; }
                    }
                }

                if (endId == ENTRY_OUT_PIN) {
                    isEndInput = false;
                    dstHash = ENTRY_NODE_ID;
                }
                else {
                    for (auto& s : _currentGraph.states) {
                        if (endId == s.stateHash + 1) { isEndInput = true; dstHash = s.stateHash; break; }
                        if (endId == s.stateHash + 2) { isEndInput = false; dstHash = s.stateHash; break; }
                    }
                }

                // ★スマートな配線解決: ユーザーが逆向きにドラッグしても、常に正しい From/To を割り出す
                uint32_t finalSrc = isStartInput ? dstHash : srcHash;
                uint32_t finalDst = isStartInput ? srcHash : dstHash;

                // UX向上: 不正な接続（In同士、Out同士、最終的な遷移先がEntry）は赤色で弾く
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
                                    AnimTransition t;
                                    t.targetStateHash = finalDst;
                                    s.transitions.push_back(t);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        // 空白リリース時の処理 (内部ステートを正常にクリアする)
        ed::PinId unusedPin;
        if (ed::QueryNewNode(&unusedPin)) {
            ed::RejectNewItem(); // 今回はノード自動生成は不要なので却下
        }
    }
    ed::EndCreate();

    // ====================================================================
    // 削除処理 (Entryは削除できないように保護)
    // ====================================================================
    if (ed::BeginDelete()) {
        ed::LinkId linkId;
        while (ed::QueryDeletedLink(&linkId)) {
            if (ed::AcceptDeletedItem()) {
                int id = (int)(uintptr_t)linkId.Get();
                if (id == ENTRY_LINK_ID) {
                    _currentGraph.entryStateHash = 0;
                }
                else {
                    int currentId = 0x00000100; // リンクIDの開始値と合わせる
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
                ed::RejectDeletedItem(); // ★ Entryノードは絶対に消させない
                continue;
            }

            if (ed::AcceptDeletedItem()) {
                if (_currentGraph.entryStateHash == targetHash) {
                    _currentGraph.entryStateHash = 0;
                }

                _currentGraph.states.erase(
                    std::remove_if(_currentGraph.states.begin(), _currentGraph.states.end(),
                        [targetHash](const AnimState& s) { return s.stateHash == targetHash; }),
                    _currentGraph.states.end());
            }
        }
    }
    ed::EndDelete();

    // 右クリックメニュー (キャンバス)
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

    int currentId = 0x00000100; // リンクIDの開始値と合わせる
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
                    trans.conditions.push_back({ CCL::Utils::HashString("Speed"), AnimConditionOp::Greater, 0.0f });
                }

                for (size_t c = 0; c < trans.conditions.size(); ++c) {
                    auto& cond = trans.conditions[c];
                    ImGui::PushID((int)c);

                    ImGui::Text("条件 (Condition) %d", (int)c);

                    int op = (int)cond.op;
                    ImGui::SetNextItemWidth(100);
                    ImGui::Combo("Op", &op, "Equal\0Greater\0Less\0\0");
                    cond.op = (AnimConditionOp)op;

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    ImGui::DragFloat("閾値 (Threshold)", &cond.threshold, 0.1f);

                    ImGui::PopID();
                }
                return;
            }
            currentId++;
        }
    }
}

void AnimGraphWindow::DrawToolbar() {
    if (ImGui::Button("保存 (Save JSON)")) SaveGraph(_currentFilePath);
    ImGui::SameLine();
    if (ImGui::Button("読込 (Load JSON)")) LoadGraph(_currentFilePath);
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
    AnimGraphSerializer::LoadFromJSON(_currentGraph, path, {});
}

void AnimGraphWindow::SaveGraph(const std::string& path) {
    AnimGraphSerializer::SaveToJSON(_currentGraph, path);
}