#include "AnimGraphWindow.h"
#include "Engine/GamePlay/Animation/AnimGraphSerializer.h"
#include "Engine/Core/Math/StringHash.h"
#include "imgui-node-editor/imgui_node_editor.h"


#include <imgui.h>

namespace ed = ax::NodeEditor;

AnimGraphWindow::AnimGraphWindow() : EditorWindow("Anim Graph Editor")
{
    // コンテキストの作成
    _nodeContext = ed::CreateEditor();
}

AnimGraphWindow::~AnimGraphWindow()
{
    if (_nodeContext) ed::DestroyEditor(_nodeContext);
}

void AnimGraphWindow::DrawContents(EditorContext& context)
{
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

    // 1. ノードの描画
    for (auto& state : _currentGraph.states) {
        ed::BeginNode(state.stateHash);

        ImGui::Text("%s", state.sequenceName.c_str());

        // 入力ピン (IDはハッシュにビット演算で一意にする)
        ed::BeginPin(state.stateHash << 1, ed::PinKind::Input);
        ImGui::Text("-> In");
        ed::EndPin();

        ImGui::SameLine();

        // 出力ピン
        ed::BeginPin((state.stateHash << 1) | 1, ed::PinKind::Output);
        ImGui::Text("Out ->");
        ed::EndPin();

        ed::EndNode();
    }

    // 2. リンクの描画
    int linkId = 1;
    for (const auto& state : _currentGraph.states) {
        for (const auto& trans : state.transitions) {
            // 出力元ピン -> 入力先ピン を繋ぐ
            ed::Link(linkId++, (state.stateHash << 1) | 1, trans.targetStateHash << 1);
        }
    }

    // 3. 新規リンク作成のハンドル
    if (ed::BeginCreate()) {
        ed::NodeId startNode, endNode;
        ed::PinId startPin, endPin;
        if (ed::QueryNewLink(&startPin, &endPin)) {
            if (ed::AcceptNewItem()) {
                // ピンIDから元のNodeHashを復元
                uint32_t srcHash = (uint32_t)startPin.Get() >> 1;
                uint32_t dstHash = (uint32_t)endPin.Get() >> 1;

                for (auto& s : _currentGraph.states) {
                    if (s.stateHash == srcHash) {
                        AnimTransition t;
                        t.targetStateHash = dstHash;
                        s.transitions.push_back(t);
                        break;
                    }
                }
            }
        }
    }
    ed::EndCreate();

    ed::End();

    // 選択状態の取得
    ed::NodeId selectedId;
    if (ed::GetSelectedNodes(&selectedId, 1) > 0) {
        _selectedNodeHash = (uint32_t)selectedId.Get();
    }
    else {
        _selectedNodeHash = 0;
    }
}

void AnimGraphWindow::DrawInspector()
{
    ImGui::Text("State Inspector");
    ImGui::Separator();

    if (_selectedNodeHash == 0) {
        ImGui::TextDisabled("Select a node to edit.");
        return;
    }

    for (auto& state : _currentGraph.states) {
        if (state.stateHash == _selectedNodeHash) {
            ImGui::Text("Hash: 0x%X", state.stateHash);
            char buf[64];
            strcpy_s(buf, state.sequenceName.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) {
                state.sequenceName = buf;
            }

            // ★ 初期ステートかどうかの表示
            if (_currentGraph.entryStateHash == state.stateHash) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[ Entry State ]");
            }
            else {
                if (ImGui::Button("Set as Entry State")) {
                    _currentGraph.entryStateHash = state.stateHash;
                }
            }
            ImGui::Separator();

            // ==============================================================
            //  このノードから伸びる「矢印(Transition)」の条件編集
            // ==============================================================
            ImGui::Text("Transitions (Out)");

            for (auto& trans : state.transitions) {
                // 遷移先の名前を探す（表示用）
                std::string targetName = "Unknown";
                for (const auto& s : _currentGraph.states) {
                    if (s.stateHash == trans.targetStateHash) targetName = s.sequenceName;
                }

                // ツリーを開いて条件を編集する
                if (ImGui::TreeNode((void*)&trans, "To: %s", targetName.c_str())) {

                    if (ImGui::Button("+ Add Condition")) {
                        // デフォルトで「Speed > 0.0」という条件を追加
                        trans.conditions.push_back({ CCL::Utils::HashString("Speed"), AnimConditionOp::Greater, 0.0f });
                    }

                    for (size_t i = 0; i < trans.conditions.size(); ++i) {
                        ImGui::PushID((int)i);
                        auto& cond = trans.conditions[i];

                        ImGui::Text("Condition %d", i);

                        // 判定の演算子を変更 (Equal, Greater, Less)
                        int op = (int)cond.op;
                        ImGui::Combo("Op", &op, "Equal\0Greater\0Less\0\0");
                        cond.op = (AnimConditionOp)op;

                        // 閾値を変更
                        ImGui::DragFloat("Threshold", &cond.threshold, 0.1f);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            }
            break;
        }
    }
}

void AnimGraphWindow::LoadGraph(const std::string& path) {
    AnimGraphSerializer::LoadFromJSON(_currentGraph, path, {});
}

void AnimGraphWindow::SaveGraph(const std::string& path) {
    AnimGraphSerializer::SaveToJSON(_currentGraph, path);
}

void AnimGraphWindow::DrawToolbar() {
    if (ImGui::Button("Load JSON")) LoadGraph(_currentFilePath);
    ImGui::SameLine();
    if (ImGui::Button("Save JSON")) SaveGraph(_currentFilePath);
    
    //ImGui::SameLine();
    //ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // ==============================================================
    // ★追加: 新規ステート（ノード）の作成UI
    // ==============================================================
    ImGui::InputText("##NewState", _newStateName, sizeof(_newStateName));
    ImGui::SameLine();
    if (ImGui::Button("+ Add State") && _newStateName[0] != '\0') {
        AnimState newState;
        newState.stateHash = CCL::Utils::HashString(_newStateName);
        newState.sequenceName = _newStateName; // 初期値として名前を入れておく

        // 最初のノードなら、それをエントリー(初期)ステートにする
        if (_currentGraph.states.empty()) {
            _currentGraph.entryStateHash = newState.stateHash;
        }

        _currentGraph.states.push_back(newState);
        _newStateName[0] = '\0'; // 入力欄をクリア
    }
}