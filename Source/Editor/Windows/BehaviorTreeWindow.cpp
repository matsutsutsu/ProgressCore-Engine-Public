/**
 * @file BehaviorTreeWindow.cpp
 * @brief BehaviorTree エディタの実装
 */
#include "BehaviorTreeWindow.h"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <algorithm>
#include <queue>
#include <fstream>
#include <json.hpp>
#include "Engine/Platform/Logger.h"
#include "Engine/Platform/Dialog.h"


using json = nlohmann::json;

 // 長い名前空間を "ed" に短縮するお作法
namespace ed = ax::NodeEditor;

// ★ どこか上部（またはヘッダー）で、IDに対応する分かりやすい名前のリストを定義しておく
static const char* ActionNames[] = {
    "0: なし (None)",
    "1: 移動 (Move To Target)",
    "2: 近接攻撃 (Melee Attack)",
    "3: 突進 (Charge Attack)"
};
static const int ActionNamesCount = IM_ARRAYSIZE(ActionNames);

static const char* ConditionNames[] = {
    "0: なし (None)",
    "1: HPが50%以下 (HP < 50)",
    "2: ターゲットが近い (Target is Near)"
};
static const int ConditionNamesCount = IM_ARRAYSIZE(ConditionNames);


BehaviorTreeWindow::BehaviorTreeWindow() : EditorWindow("Behavior Tree Editor") {
    _nodeContext = ed::CreateEditor();

    // 初期状態としてRootノードを1つだけ作成しておく
    CreateNode(EditorBTNodeType::Root, "ROOT", 0.0f, 0.0f);
}

BehaviorTreeWindow::~BehaviorTreeWindow() {
    if (_nodeContext) ed::DestroyEditor(_nodeContext);
}

void BehaviorTreeWindow::CreateNode(EditorBTNodeType type, const std::string& name, float x, float y) {
    EditorBTNode node;
    node.id = GetNextId();
    node.type = type;
    node.name = name;

    // グループとルート以外は入力ピンを持つ
    if (type != EditorBTNodeType::Root && type != EditorBTNodeType::Group) {
        node.inputPin = { GetNextId(), node.id, true };
    }
    else {
        node.inputPin.id = 0;
    }

    // アクション/条件/グループ 以外は出力ピンを持つ
    if (type != EditorBTNodeType::Condition && type != EditorBTNodeType::Action && type != EditorBTNodeType::Group) {
        node.outputPin = { GetNextId(), node.id, false };
    }
    else {
        node.outputPin.id = 0;
    }

    node.posX = x;
    node.posY = y;
    node.positionInitialized = false;
    _nodes.push_back(node);
}

void BehaviorTreeWindow::DrawContents(EditorContext& context) {
    ed::SetCurrentEditor(_nodeContext);

    // カラムを分けて、左側をノードエディタ、右側をインスペクタにする（AnimGraphと同様）
    ImGui::Columns(2, "BTEditorColumns", true);
    if (ImGui::GetColumnWidth(0) < 100.0f) ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.75f);

    DrawToolbar();
    DrawNodeWorkspace();

    ImGui::NextColumn();
    DrawInspector();
    ImGui::Columns(1);
}

// ====================================================================
// 描画：ツールバー（「日本語（英語名）」ボタン）
// ====================================================================
void BehaviorTreeWindow::DrawToolbar() {
    if (ImGui::Button("別名で保存 (Save As...)")) {
        char path[MAX_PATH] = "";
        if (Dialog::SaveFileName(path, MAX_PATH, "BehaviorTree (*.json)\0*.json\0", "AIグラフを保存", "json") == DialogResult::OK) {
            _currentFilePath = path;
            SaveGraph(_currentFilePath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("開く (Open...)")) {
        char path[MAX_PATH] = "";
        if (Dialog::OpenFileName(path, MAX_PATH, "BehaviorTree (*.json)\0*.json\0", "AIグラフを開く") == DialogResult::OK) {
            _currentFilePath = path;
            LoadGraph(_currentFilePath);
        }
    }

    ImGui::Separator();
    
    // ノード追加ボタン
    if (ImGui::Button("+ セレクター (Selector)")) 
        CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", 200, 0); 
    ImGui::SameLine();
    if (ImGui::Button("+ シーケンス (Sequence)")) 
        CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", 200, 50); 
    ImGui::SameLine();
    if (ImGui::Button("+ コンディション (Condition)")) 
        CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", 400, 0); 
    ImGui::SameLine();
    if (ImGui::Button("+ アクション (Action)")) 
        CreateNode(EditorBTNodeType::Action, "アクション (Action)", 400, 50);
}

/**
 * @brief 毎フレーム、親子関係から実行優先順位を計算する
 * @note DOD（データ指向設計）において、配列の並び順はキャッシュ効率に直結する重要な情報である
 */
void BehaviorTreeWindow::UpdateExecutionOrder() {
    // 全ての順番を一旦リセット
    for (auto& n : _nodes) n.executionOrder = -1;

    // 1. 各親ノードに対して、接続されている子を取得
    for (auto& parent : _nodes) {
        if (parent.outputPin.id == 0) continue;

        std::vector<EditorBTNode*> children;
        for (const auto& link : _links) {
            if (link.startPinId == parent.outputPin.id) {
                for (auto& child : _nodes) {
                    if (child.inputPin.id == link.endPinId) {
                        children.push_back(&child);
                    }
                }
            }
        }

        // 2. Y座標でソート（Bakeロジックと一致させる）
        std::sort(children.begin(), children.end(), [](EditorBTNode* a, EditorBTNode* b) {
            return a->posY < b->posY;
            });

        // 3. 順序番号を割り当てる（1から開始）
        for (int i = 0; i < (int)children.size(); ++i) {
            children[i]->executionOrder = i + 1;
        }
    }
}

/**
 * @brief ノードエディタ空間（キャンバス）の描画と、リンク操作の監視を行う
 */
void BehaviorTreeWindow::DrawNodeWorkspace() {
    UpdateExecutionOrder();

    ed::Begin("BT_Node_Workspace");

    for (auto& node : _nodes) {
        if (!node.positionInitialized) {
            ed::SetNodePosition(node.id, ImVec2(node.posX, node.posY));
            node.positionInitialized = true;
        }

        // ★追加: グループノードの場合の特殊なスタイル適用
        if (node.type == EditorBTNodeType::Group) {
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(255, 255, 255, 30));     // 半透明
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ImColor(255, 255, 255, 50)); // 枠線
        }

        ed::BeginNode(node.id);

        if (node.executionOrder != -1 && node.type != EditorBTNodeType::Group) {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "[%d]", node.executionOrder);
            ImGui::SameLine();
        }

        // ★ 修正：ノードの種類に応じて、キャンバス上の表示名を動的に変える
        if (node.type == EditorBTNodeType::Action && node.actionOrConditionId < ActionNamesCount) {
            // 例: 「Action」という基本名の下に、具体的な行動名を表示
            ImGui::TextUnformatted("行動 (Action)");
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "[ %s ]", ActionNames[node.actionOrConditionId]);
        }
        else if (node.type == EditorBTNodeType::Condition && node.actionOrConditionId < ConditionNamesCount) {
            ImGui::TextUnformatted("条件 (Condition)");
            ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "[ %s ]", ConditionNames[node.actionOrConditionId]);
        }
        else {
            ImGui::TextUnformatted(node.name.c_str());
        }

        ImGui::TextUnformatted(node.name.c_str());

        // ★追加: グループノードの場合は枠を広げる（ピンは描画しない）
        if (node.type == EditorBTNodeType::Group) {
            ed::Group(ImVec2(node.width, node.height));
        }
        else {
            ImGui::Spacing();
            if (node.inputPin.id != 0) {
                ed::BeginPin(node.inputPin.id, ed::PinKind::Input);
                ImGui::Text("-> In");
                ed::EndPin();
            }
            if (node.outputPin.id != 0) {
                ImGui::SameLine(100.0f);
                ed::BeginPin(node.outputPin.id, ed::PinKind::Output);
                ImGui::Text("Out ->");
                ed::EndPin();
            }
        }

        ed::EndNode();

        if (node.type == EditorBTNodeType::Group) {
            ed::PopStyleColor(2);
        }

        ImVec2 p = ed::GetNodePosition(node.id);
        node.posX = p.x;
        node.posY = p.y;
    }

    for (auto& link : _links) {
        ed::Link(link.id, link.startPinId, link.endPinId);
    }

    if (ed::BeginCreate()) {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            if (startPinId && endPinId) {
                if (ed::AcceptNewItem()) {
                    BTLink newLink;
                    newLink.id = GetNextId();
                    newLink.startPinId = startPinId.Get();
                    newLink.endPinId = endPinId.Get();
                    _links.push_back(newLink);
                }
            }
        }
    }
    ed::EndCreate();

    if (ed::BeginDelete()) {
        ed::NodeId deletedNodeId = 0;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            if (ed::AcceptDeletedItem()) {
                DeleteNodeAndLinks(deletedNodeId.Get());
            }
        }
        ed::LinkId deletedLinkId = 0;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                _links.erase(std::remove_if(_links.begin(), _links.end(),
                    [&](const BTLink& l) { return l.id == deletedLinkId.Get(); }), _links.end());
            }
        }
    }
    ed::EndDelete();

    HandleContextMenu();
    ed::End();

    if (_needInitialZoom) {
        ed::NavigateToContent();
        _needInitialZoom = false;
    }
}

// ====================================================================
// インスペクタ（右画面）の描画処理
// ====================================================================
void BehaviorTreeWindow::DrawInspector() {
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));

    if (nodeCount > 0) {
        uint32_t selectedId = selectedNodes[0].Get();
        auto it = std::find_if(_nodes.begin(), _nodes.end(), [&](const EditorBTNode& n) { return n.id == selectedId; });

        if (it != _nodes.end()) {
            // ★機能追加: ノード名の変更（コメントや整理に必須）
            char nameBuf[256];
            strcpy_s(nameBuf, it->name.c_str());
            if (ImGui::InputText("名前 (Name)", nameBuf, sizeof(nameBuf))) {
                it->name = nameBuf;
            }



            // ---------------------------------------------------------
            // インスペクタ描画部分
            // ---------------------------------------------------------
            if (it->type == EditorBTNodeType::Condition) {
                ImGui::Text("条件設定:");
                int currentItem = static_cast<int>(it->actionOrConditionId);

                // 数字入力ではなく、ドロップダウン（Combo）に変更
                if (ImGui::Combo("条件 (Condition)", &currentItem, ConditionNames, ConditionNamesCount)) {
                    it->actionOrConditionId = static_cast<ActionID>(currentItem);
                }
            }
            else if (it->type == EditorBTNodeType::Action) {
                ImGui::Text("行動設定:");
                int currentItem = static_cast<int>(it->actionOrConditionId);

                // ドロップダウン（Combo）に変更
                if (ImGui::Combo("行動 (Action)", &currentItem, ActionNames, ActionNamesCount)) {
                    it->actionOrConditionId = static_cast<ActionID>(currentItem);
                }
            }

            // ★機能追加: グループノードの枠サイズ調整
            if (it->type == EditorBTNodeType::Group) {
                ImGui::Separator();
                ImGui::DragFloat("枠の幅 (Width)", &it->width, 1.0f, 100.0f, 2000.0f);
                ImGui::DragFloat("枠の高さ (Height)", &it->height, 1.0f, 100.0f, 2000.0f);
            }
        }
    }
    else {
        ImGui::TextDisabled("編集するノードを選択してください。");
    }
}


// ====================================================================
// ノードと関連リンクの安全な連鎖削除
// ====================================================================
void BehaviorTreeWindow::DeleteNodeAndLinks(uint32_t nodeId) {
    // 1. 削除対象のノードを探す
    auto it = std::find_if(_nodes.begin(), _nodes.end(), [nodeId](const EditorBTNode& n) { return n.id == nodeId; });
    if (it == _nodes.end()) return;

    // 2. そのノードが持っているピンのIDを取得
    uint32_t inPinId = it->inputPin.id;
    uint32_t outPinId = it->outputPin.id;

    // 3. このピンに繋がっているリンクを全て配列から消し去る
    _links.erase(std::remove_if(_links.begin(), _links.end(),
        [inPinId, outPinId](const BTLink& link) {
            return link.startPinId == outPinId || link.endPinId == inPinId;
        }), _links.end());

    // 4. 最後にノード本体を消す
    _nodes.erase(it);

    CCL_LOG_INFO(LogCategory::Editor, "Node(ID:%u) and its connected links were deleted.", nodeId);
}

// ====================================================================
// 右クリックコンテキストメニュー処理
// ====================================================================
void BehaviorTreeWindow::HandleContextMenu() {
    ed::Suspend();

    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("Create New Node");
        _newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("Create New Node")) {
        ImVec2 canvasPos = ed::ScreenToCanvas(_newNodeSpawnPos);

        if (ImGui::MenuItem("セレクター (Selector)")) CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("シーケンス (Sequence)")) CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", canvasPos.x, canvasPos.y);
        ImGui::Separator();
        if (ImGui::MenuItem("コンディション (Condition)")) CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("アクション (Action)")) CreateNode(EditorBTNodeType::Action, "アクション (Action)", canvasPos.x, canvasPos.y);
        ImGui::Separator();
        // ★追加: グループノード生成
        if (ImGui::MenuItem("コメントグループ (Group)")) CreateNode(EditorBTNodeType::Group, "コメント (Comment)", canvasPos.x, canvasPos.y);

        ImGui::Separator();
        // ★追加: 複数ノードの複製
        if (ImGui::MenuItem("選択ノードを複製 (Duplicate)")) {
            DuplicateSelectedNodes();
        }

        ImGui::EndPopup();
    }

    ed::Resume();
}

// ====================================================================
// セーブ処理：ピンIDを明示的に保存するように拡張
// ====================================================================
void BehaviorTreeWindow::SaveGraph(const std::string& path) {
    json root;
    ed::SetCurrentEditor(_nodeContext);

    // ノード情報の保存
    json jNodes = json::array();
    for (auto& node : _nodes) {
        json jn;
        jn["id"] = node.id;
        jn["type"] = static_cast<int>(node.type);
        jn["name"] = node.name;
        jn["actionId"] = static_cast<int>(node.actionOrConditionId);

        // ★ 修正：ピンのIDを保存。これがリンクの復元に不可欠。
        jn["inputPinId"] = node.inputPin.id;
        jn["outputPinId"] = node.outputPin.id;

        ImVec2 pos = ed::GetNodePosition(node.id);
        jn["posX"] = pos.x;
        jn["posY"] = pos.y;
        // ★追加: グループサイズを保存
        if (node.type == EditorBTNodeType::Group) {
            jn["width"] = node.width;
            jn["height"] = node.height;
        }
        jNodes.push_back(jn);
    }
    root["editor_nodes"] = jNodes;

    // リンク情報の保存
    json jLinks = json::array();
    for (auto& link : _links) {
        json jl;
        jl["id"] = link.id;
        jl["startPin"] = link.startPinId;
        jl["endPin"] = link.endPinId;
        jLinks.push_back(jl);
    }
    root["editor_links"] = jLinks;
    root["last_id_counter"] = _idCounter;

    // 書き出し
    std::ofstream o(path);
    if (o.is_open()) {
        o << root.dump(4);
        CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフを保存しました: %s", path.c_str());
    }

    // ついでにランタイム用もBakeする
    CompileAndSave(path);
}

// ====================================================================
// ロード処理：ピンIDを生成せず、保存された値で復元する
// ====================================================================
void BehaviorTreeWindow::LoadGraph(const std::string& path) {
    std::ifstream i(path);
    if (!i.is_open()) return;

    json j;
    i >> j;

    _nodes.clear();
    _links.clear();
    _idCounter = j.value("last_id_counter", 1);

    // ノードの復元
    for (auto& jn : j["editor_nodes"]) {
        EditorBTNode node;
        node.id = jn["id"];
        node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
        node.name = jn["name"];
        node.actionOrConditionId = static_cast<ActionID>(jn["actionId"].get<int>());
        node.posX = jn["posX"];
        node.posY = jn["posY"];
        // ★追加: グループサイズを復元
        if (node.type == EditorBTNodeType::Group) {
            node.width = jn.value("width", 300.0f);
            node.height = jn.value("height", 200.0f);
        }

        // ★ 修正：GetNextId() を使わず、保存されたIDをそのままセットする
        node.inputPin = { jn.value("inputPinId", 0u), node.id, true };
        node.outputPin = { jn.value("outputPinId", 0u), node.id, false };

        _nodes.push_back(node);
    }

    // リンクの復元
    for (auto& jl : j["editor_links"]) {
        BTLink link;
        link.id = jl["id"];
        link.startPinId = jl["startPin"];
        link.endPinId = jl["endPin"];
        _links.push_back(link);
    }

    _needInitialZoom = true;
    CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフをロードしました: %s", path.c_str());
}

// ====================================================================
// グラフのコンパイルと保存（Bake処理）
// ====================================================================
void BehaviorTreeWindow::CompileAndSave(const std::string& path) {
    if (_nodes.empty()) return;

    // 1. Rootノードを探す
    EditorBTNode* rootNode = nullptr;
    for (auto& n : _nodes) {
        if (n.type == EditorBTNodeType::Root) {
            rootNode = &n;
            break;
        }
    }

    if (!rootNode) {
        CCL_LOG_ERROR(LogCategory::Editor, "[BT Editor] Root node not found. Bake failed.");
        return;
    }

    // ---------------------------------------------------------
    // ヘルパー：特定の出力ピンから繋がっている子ノードを「Y座標順（上から下）」に取得する
    // ---------------------------------------------------------
    auto getSortedChildren = [&](uint32_t outputPinId) {
        std::vector<EditorBTNode*> children;
        for (const auto& link : _links) {
            if (link.startPinId == outputPinId) {
                for (auto& n : _nodes) {
                    if (n.inputPin.id == link.endPinId) {
                        children.push_back(&n);
                        break;
                    }
                }
            }
        }
        // エディタ上のY座標でソート（人間が配置した上からの順番を、実行順序とする）
        std::sort(children.begin(), children.end(), [](EditorBTNode* a, EditorBTNode* b) {
            return a->posY < b->posY;
            });
        return children;
        };

    // Rootに繋がっている最初のノード群を取得（通常は1つ。Selector等）
    auto rootChildren = getSortedChildren(rootNode->outputPin.id);
    if (rootChildren.empty()) {
        CCL_LOG_WARN(LogCategory::Editor, "[BT Editor] Root is not connected to anything.");
        return;
    }

    // ---------------------------------------------------------
    // 2. BFS（幅優先探索）によるDODフラット配列の構築
    // ---------------------------------------------------------
    nlohmann::json jsonNodes = nlohmann::json::array();

    // BFS用のタスクキュー (対象ノード, 確定したJSON配列のインデックス)
    struct BakeTask {
        EditorBTNode* editorNode;
        int flatIndex;
    };
    std::queue<BakeTask> tasks;

    // エディタの型からランタイムの型文字列への変換ヘルパー
    auto getRuntimeTypeString = [](EditorBTNodeType type) {
        switch (type) {
        case EditorBTNodeType::Selector:  return "Selector";
        case EditorBTNodeType::Sequence:  return "Sequence";
        case EditorBTNodeType::Condition: return "Condition";
        case EditorBTNodeType::Action:    return "Action";
        default: return "Unknown";
        }
        };

    // 最初の子（本当のツリーの根）をキューと配列に入れる
    EditorBTNode* actualRoot = rootChildren[0];

    nlohmann::json rootJson;
    rootJson["type"] = getRuntimeTypeString(actualRoot->type);
    rootJson["childCount"] = 0;       // 後で埋める
    rootJson["firstChildOffset"] = 0; // 後で埋める
    rootJson["actionOrConditionId"] = static_cast<int>(actualRoot->actionOrConditionId);
    jsonNodes.push_back(rootJson);

    tasks.push({ actualRoot, 0 });

    // キューが空になるまで階層ごとに処理
    while (!tasks.empty()) {
        BakeTask task = tasks.front();
        tasks.pop();

        // このノードの子供を取得（Y座標でソート済み）
        auto children = getSortedChildren(task.editorNode->outputPin.id);

        if (children.empty()) continue;

        // 親ノードのオフセットとカウントを「現在の配列サイズ」で確定させる
        jsonNodes[task.flatIndex]["childCount"] = children.size();
        jsonNodes[task.flatIndex]["firstChildOffset"] = jsonNodes.size();

        // 子供たちを配列に連続して追加し、次の探索キューに入れる
        for (EditorBTNode* child : children) {
            nlohmann::json childJson;
            childJson["type"] = getRuntimeTypeString(child->type);
            childJson["childCount"] = 0;
            childJson["firstChildOffset"] = 0;
            childJson["actionOrConditionId"] = static_cast<int>(child->actionOrConditionId);

            jsonNodes.push_back(childJson);
            tasks.push({ child, static_cast<int>(jsonNodes.size() - 1) });
        }
    }

    // ---------------------------------------------------------
    // 3. JSONファイルへの出力
    // ---------------------------------------------------------
    nlohmann::json rootDoc;
    rootDoc["assetId"] = 1; // アセットID（今回は固定）
    rootDoc["nodes"] = jsonNodes;

    std::ofstream file(path);
    if (file.is_open()) {
        file << rootDoc.dump(4); // 4マスのインデントで綺麗に保存
        file.close();
        CCL_LOG_SUCCESS(LogCategory::Editor, "[BT Editor] Bake Successful! Saved to: %s", path.c_str());
        CCL_LOG_INFO(LogCategory::Editor, "[BT Editor] Total Nodes Baked: %zu", jsonNodes.size());
    }
    else {
        CCL_LOG_ERROR(LogCategory::Editor, "[BT Editor] Failed to open file for writing: %s", path.c_str());
    }
}

// ====================================================================
// 選択されたノードと、その間のリンクをまとめて複製する高度な機能
// ====================================================================
void BehaviorTreeWindow::DuplicateSelectedNodes() {
    std::vector<ed::NodeId> selectedNodeIds;
    selectedNodeIds.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodeIds.data(), static_cast<int>(selectedNodeIds.size()));

    if (nodeCount == 0) return;

    // 古いIDから新しいIDへのマッピングを記録（リンクの再接続に使う）
    std::map<uint32_t, uint32_t> oldToNewPinId;

    std::vector<EditorBTNode> newNodes;

    // 1. ノードの複製
    for (int i = 0; i < nodeCount; ++i) {
        uint32_t oldId = selectedNodeIds[i].Get();
        auto it = std::find_if(_nodes.begin(), _nodes.end(), [oldId](const EditorBTNode& n) { return n.id == oldId; });

        if (it != _nodes.end()) {
            EditorBTNode newNode = *it;
            newNode.id = GetNextId();
            newNode.posX += 50.0f; // 重ならないように右下に少しずらす
            newNode.posY += 50.0f;
            newNode.positionInitialized = false;
            newNode.executionOrder = -1;

            // 新しいピンIDの割り当てとマッピング
            if (newNode.inputPin.id != 0) {
                uint32_t newIn = GetNextId();
                oldToNewPinId[newNode.inputPin.id] = newIn;
                newNode.inputPin.id = newIn;
                newNode.inputPin.nodeId = newNode.id;
            }
            if (newNode.outputPin.id != 0) {
                uint32_t newOut = GetNextId();
                oldToNewPinId[newNode.outputPin.id] = newOut;
                newNode.outputPin.id = newOut;
                newNode.outputPin.nodeId = newNode.id;
            }

            newNodes.push_back(newNode);
        }
    }

    // 2. リンクの複製（複製されたノード同士が繋がっていた場合のみ）
    std::vector<BTLink> newLinks;
    for (const auto& link : _links) {
        if (oldToNewPinId.count(link.startPinId) && oldToNewPinId.count(link.endPinId)) {
            BTLink newLink;
            newLink.id = GetNextId();
            newLink.startPinId = oldToNewPinId[link.startPinId];
            newLink.endPinId = oldToNewPinId[link.endPinId];
            newLinks.push_back(newLink);
        }
    }

    // 3. コピーした要素を世界に反映
    _nodes.insert(_nodes.end(), newNodes.begin(), newNodes.end());
    _links.insert(_links.end(), newLinks.begin(), newLinks.end());

    // 操作完了後、選択状態を解除する
    ed::ClearSelection();
    CCL_LOG_INFO(LogCategory::Editor, "%d nodes duplicated.", nodeCount);
}