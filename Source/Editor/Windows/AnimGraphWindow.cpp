//#include "AnimGraphWindow.h"
//#include "Engine/GamePlay/Animation/AnimGraphSerializer.h" // リファクタ後のパス
//#include "Engine/Core/Math/StringHash.h" // CCL::Utils::HashString
//#include <imnodes.h>
//#include <imgui.h>
//
//AnimGraphWindow::AnimGraphWindow() : EditorWindow("Anim Graph Editor")
//{
//    // imnodesのコンテキストを初期化
//    _nodeContext = ImNodes::CreateContext();
//    ImNodes::SetCurrentContext(_nodeContext);
//}
//
//AnimGraphWindow::~AnimGraphWindow()
//{
//    if (_nodeContext) {
//        ImNodes::DestroyContext(_nodeContext);
//    }
//}
//
//void AnimGraphWindow::DrawContents(EditorContext& context)
//{
//    ImNodes::SetCurrentContext(_nodeContext);
//
//    // 上部ツールバー
//    DrawToolbar();
//
//    // 画面分割: 左側(ノードエディタ 70%)、右側(インスペクタ 30%)
//    ImGui::Columns(2, "AnimGraphColumns");
//    ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.7f);
//
//    // 左ペイン：ノード空間
//    DrawNodeWorkspace();
//
//    ImGui::NextColumn();
//
//    // 右ペイン：選択項目の詳細編集
//    DrawInspector();
//
//    ImGui::Columns(1);
//}
//
//void AnimGraphWindow::DrawToolbar()
//{
//    if (ImGui::Button("Load")) {
//        // ※実際は Dialog::OpenFileName 等を使用してください
//        LoadGraph(_currentFilePath);
//    }
//    ImGui::SameLine();
//    if (ImGui::Button("Save")) {
//        SaveGraph(_currentFilePath);
//    }
//    ImGui::SameLine();
//
//    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
//    ImGui::SameLine();
//
//    // 新規ステートの追加
//    ImGui::InputText("##NewState", _newStateName, sizeof(_newStateName));
//    ImGui::SameLine();
//    if (ImGui::Button("+ Add State") && _newStateName[0] != '\0') {
//        CCL::ECS::AnimState newState;
//        newState.stateHash = CCL::Utils::HashString(_newStateName);
//        newState.sequenceName = _newStateName; // 仮
//        _currentGraph.states.push_back(newState);
//        _newStateName[0] = '\0'; // クリア
//    }
//    ImGui::Separator();
//}
//
//void AnimGraphWindow::DrawNodeWorkspace()
//{
//    ImNodes::BeginNodeEditor();
//
//    // 1. ノード(State)の描画
//    for (auto& state : _currentGraph.states) {
//        int nodeId = static_cast<int>(state.stateHash); // 簡易的にHashをIDとして使用
//
//        ImNodes::BeginNode(nodeId);
//
//        // タイトルバー
//        ImNodes::BeginNodeTitleBar();
//        ImGui::TextUnformatted(state.sequenceName.c_str());
//        ImNodes::EndNodeTitleBar();
//
//        // 入力ピン (左側)
//        ImNodes::BeginInputAttribute(nodeId << 1); // ピンIDは被らないようにシフト
//        ImGui::Text("In");
//        ImNodes::EndInputAttribute();
//
//        // 出力ピン (右側)
//        ImNodes::BeginOutputAttribute((nodeId << 1) | 1);
//        ImGui::Indent(40);
//        ImGui::Text("Out");
//        ImNodes::EndOutputAttribute();
//
//        ImNodes::EndNode();
//    }
//
//    // 2. リンク(Transition)の描画
//    int linkIdCounter = 100000;
//    for (const auto& state : _currentGraph.states) {
//        int startNodeId = static_cast<int>(state.stateHash);
//        int outPinId = (startNodeId << 1) | 1;
//
//        for (const auto& transition : state.transitions) {
//            int targetNodeId = static_cast<int>(transition.targetStateHash);
//            int inPinId = targetNodeId << 1;
//
//            ImNodes::Link(linkIdCounter++, outPinId, inPinId);
//        }
//    }
//
//    ImNodes::EndNodeEditor();
//
//    // --- インタラクションの取得 ---
//    // どのノードが選択されたか
//    int numSelectedNodes = ImNodes::NumSelectedNodes();
//    if (numSelectedNodes > 0) {
//        std::vector<int> selectedNodes(numSelectedNodes);
//        ImNodes::GetSelectedNodes(selectedNodes.data());
//        _selectedNodeId = selectedNodes[0];
//    }
//    else {
//        _selectedNodeId = -1;
//    }
//
//    // 新規リンク(ドラッグ＆ドロップ)の作成判定
//    int startAttr, endAttr;
//    if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
//        int sourceNodeId = startAttr >> 1;
//        int targetNodeId = endAttr >> 1;
//
//        // 対象のステートを探してTransitionを追加
//        for (auto& state : _currentGraph.states) {
//            if (static_cast<int>(state.stateHash) == sourceNodeId) {
//                CCL::ECS::AnimTransition newTrans;
//                newTrans.targetStateHash = static_cast<uint32_t>(targetNodeId);
//                state.transitions.push_back(newTrans);
//                break;
//            }
//        }
//    }
//}
//
//void AnimGraphWindow::DrawInspector()
//{
//    ImGui::Text("Inspector");
//    ImGui::Separator();
//
//    if (_selectedNodeId == -1) {
//        ImGui::TextDisabled("No State Selected.");
//        return;
//    }
//
//    // 選択されたノード(State)を検索
//    CCL::ECS::AnimState* selectedState = nullptr;
//    for (auto& state : _currentGraph.states) {
//        if (static_cast<int>(state.stateHash) == _selectedNodeId) {
//            selectedState = &state;
//            break;
//        }
//    }
//
//    if (selectedState) {
//        ImGui::Text("State: %s", selectedState->sequenceName.c_str());
//
//        // 再生するアニメーション名（シーケンス）の編集
//        char seqBuf[256];
//        strncpy_s(seqBuf, selectedState->sequenceName.c_str(), sizeof(seqBuf));
//        if (ImGui::InputText("Target Sequence", seqBuf, sizeof(seqBuf))) {
//            selectedState->sequenceName = seqBuf;
//        }
//
//        ImGui::Separator();
//        ImGui::Text("Transitions");
//
//        // 遷移条件の編集（簡易版）
//        for (size_t i = 0; i < selectedState->transitions.size(); ++i) {
//            auto& trans = selectedState->transitions[i];
//
//            // 遷移先の名前を取得（表示用）
//            std::string targetName = "Unknown";
//            for (const auto& s : _currentGraph.states) {
//                if (s.stateHash == trans.targetStateHash) targetName = s.sequenceName;
//            }
//
//            if (ImGui::TreeNode((void*)(intptr_t)i, "To: %s", targetName.c_str())) {
//                if (ImGui::Button("Add Condition")) {
//                    trans.conditions.push_back({ CCL::Utils::HashString("Speed"), CCL::ECS::AnimConditionOp::Greater, 0.0f });
//                }
//
//                for (size_t c = 0; c < trans.conditions.size(); ++c) {
//                    auto& cond = trans.conditions[c];
//                    ImGui::PushID((int)c);
//
//                    // パラメータ名（実際は文字列のハッシュなので、エディタ用には文字列から再ハッシュする工夫が必要ですが、ここでは直接数値を編集）
//                    ImGui::Text("Condition %d", c);
//                    int op = (int)cond.op;
//                    ImGui::Combo("Op", &op, "Equal\0Greater\0Less\0\0");
//                    cond.op = (CCL::ECS::AnimConditionOp)op;
//                    ImGui::DragFloat("Threshold", &cond.threshold, 0.1f);
//
//                    ImGui::PopID();
//                }
//                ImGui::TreePop();
//            }
//        }
//    }
//}
//
//void AnimGraphWindow::LoadGraph(const std::string& path)
//{
//    // ダミーのリスト（本来はResourceManagerから取得）
//    std::vector<CCL::ECS::AnimSequence> dummyList;
//    AnimGraphSerializer::LoadFromJSON(_currentGraph, path, dummyList);
//}
//
//void AnimGraphWindow::SaveGraph(const std::string& path)
//{
//    AnimGraphSerializer::SaveToJSON(_currentGraph, path);
//}