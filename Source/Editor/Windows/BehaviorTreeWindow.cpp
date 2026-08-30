/**
 * @file BehaviorTreeWindow.cpp
 * @brief BehaviorTree エディタの実装
 */
#include "BehaviorTreeWindow.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <algorithm>
#include <queue>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <json.hpp>
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"


using json = nlohmann::json;

// 長い名前空間を "ed" に短縮するお作法
namespace ed = ax::NodeEditor;

namespace {
/// @brief node-editor が返すID(uintptr_t)を、こちら側のID型(uint32_t)へ戻す。
/// @note  EditorBTNode::id / BTPin::id はどちらも uint32_t で発行しており、それを
///        ライブラリに預けているだけなので、切り詰めても情報は落ちない。
///        各所で素の static_cast を書くと「なぜ安全か」が散らばるため1箇所に集める。
inline uint32_t ToEditorId(uintptr_t id) { return static_cast<uint32_t>(id); }
} // namespace


// グラフの構造変化を記録する専用コマンド
class BTGraphChangeCommand : public IEditorCommand {
    BehaviorTreeWindow* m_window;
    nlohmann::json m_oldState;
    nlohmann::json m_newState;
    std::string m_name;
public:
    BTGraphChangeCommand(BehaviorTreeWindow* window, const std::string& name, const nlohmann::json& oldState, const nlohmann::json& newState)
        : m_window(window), m_name(name), m_oldState(oldState), m_newState(newState) {
    }

    void Execute() override {
        m_window->RestoreGraphState(m_newState);
        CCL_LOG_INFO(LogCategory::Editor, "[BT Editor] Redo: %s", m_name.c_str());
    }
    void Undo() override {
        m_window->RestoreGraphState(m_oldState);
        CCL_LOG_INFO(LogCategory::Editor, "[BT Editor] Undo: %s", m_name.c_str());
    }
};

void BehaviorTreeWindow::RecordStateForUndo(const std::string& commandName) {
    nlohmann::json newState = GetGraphStateAsJson();
    // 状態が変わっていればコマンドをスタックに積む
    if (m_lastSavedState != newState) {
        EditorCommandHistory::Instance().ExecuteCommand(
            std::make_unique<BTGraphChangeCommand>(this, commandName, m_lastSavedState, newState)
        );
        m_lastSavedState = newState;
    }
}

// ====================================================================
// ActionRegistry ヘルパー（最小構成 BossAction と同期）
// ====================================================================
const char* BehaviorTreeWindow::GetActionDisplayName(ActionID id)
{
    if (const ActionEntry* e = FindActionEntry(id))
        return e->name;
    static thread_local char buf[64];
    snprintf(buf, sizeof(buf), "不明なAction (ID:%d)", static_cast<int>(id));
    return buf;
}

const char* BehaviorTreeWindow::GetConditionDisplayName(ActionID id)
{
    if (const ActionEntry* e = FindConditionEntry(id))
        return e->name;
    static thread_local char buf[64];
    snprintf(buf, sizeof(buf), "不明なCondition (ID:%d)", static_cast<int>(id));
    return buf;
}

bool BehaviorTreeWindow::DrawActionCombo(const char* label, ActionID& currentId)
{
    bool changed = false;
    const char* preview = GetActionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview)) {
        const char* lastCategory = nullptr;
        for (int i = 0; i < g_ActionRegistryCount; ++i) {
            const ActionEntry& e = g_ActionRegistry[i];
            if (e.category && (!lastCategory || std::strcmp(e.category, lastCategory) != 0)) {
                ImGui::SeparatorText(e.category);
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected)) {
                currentId = e.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool BehaviorTreeWindow::DrawConditionCombo(const char* label, ActionID& currentId)
{
    bool changed = false;
    const char* preview = GetConditionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview)) {
        const char* lastCategory = nullptr;
        for (int i = 0; i < g_ConditionRegistryCount; ++i) {
            const ActionEntry& e = g_ConditionRegistry[i];
            if (e.category && (!lastCategory || std::strcmp(e.category, lastCategory) != 0)) {
                ImGui::SeparatorText(e.category);
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected)) {
                currentId = e.id;
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}


BehaviorTreeWindow::BehaviorTreeWindow() : EditorWindow("Behavior Tree Editor") {

    // =========================================================================
    // ini への保存を無効化する。前回クラッシュ時の不正なノード座標が残ると復帰できないため。
    // =========================================================================
    ed::Config config;
    config.SettingsFile = nullptr; // これにより、常に初期状態でクリーンに起動する
    m_nodeContext        = ed::CreateEditor(&config);

    // 初期状態としてRootノードを1つだけ作成しておく
    CreateNode(EditorBTNodeType::Root, "ROOT", 0.0f, 0.0f);
}

BehaviorTreeWindow::~BehaviorTreeWindow() {
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}

// ====================================================================
// Undo / Redo 用：現在のグラフ状態をJSONとして抽出する
// ====================================================================
nlohmann::json BehaviorTreeWindow::GetGraphStateAsJson() {
    nlohmann::json root;
    if (m_nodeContext) {
        ed::SetCurrentEditor(m_nodeContext);
    }

    nlohmann::json jNodes = nlohmann::json::array();
    for (auto& node : m_nodes) {
        nlohmann::json jn;
        jn["id"] = node.id;
        jn["type"] = static_cast<int>(node.type);
        jn["name"] = node.name;
        jn["actionId"] = static_cast<int>(node.actionOrConditionId);

        // ピンのID
        jn["inputPinId"] = node.inputPin.id;
        jn["outputPinId"] = node.outputPin.id;

        // 座標情報の取得
        if (node.positionInitialized) {
            ImVec2 pos = ed::GetNodePosition(node.id);
            jn["posX"] = pos.x;
            jn["posY"] = pos.y;
        }
        else {
            jn["posX"] = node.posX;
            jn["posY"] = node.posY;
        }

        // グループ固有のデータ
        if (node.type == EditorBTNodeType::Group) {
            jn["width"] = node.width;
            jn["height"] = node.height;
        }

        // デコレーター固有のデータ
        if (node.type == EditorBTNodeType::Decorator) {
            jn["decoratorType"] = node.decoratorType;
            jn["decoratorParam"] = node.decoratorParam;
        }

        jNodes.push_back(jn);
    }
    root["editor_nodes"] = jNodes;

    nlohmann::json jLinks = nlohmann::json::array();
    for (auto& link : m_links) {
        nlohmann::json jl;
        jl["id"] = link.id;
        jl["startPin"] = link.startPinId;
        jl["endPin"] = link.endPinId;
        jLinks.push_back(jl);
    }
    root["editor_links"] = jLinks;
    root["last_id_counter"] = m_idCounter;

    return root;
}

// ====================================================================
// Undo / Redo 用：渡されたJSONからグラフ状態を強制復元する
// ====================================================================
void BehaviorTreeWindow::RestoreGraphState(const nlohmann::json& state) {
    if (!state.contains("editor_nodes")) return;

    // =========================================================
    // 破棄する前に、現在の選択状態を記憶しておく
    // =========================================================
    std::vector<ed::NodeId> selectedNodes;
    if (m_nodeContext) {
        ed::SetCurrentEditor(m_nodeContext);
        selectedNodes.resize(ed::GetSelectedObjectCount());
        int count = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
        selectedNodes.resize(count);
    }

    // 現在の状態を破棄
    m_nodes.clear();
    m_links.clear();

    if (m_nodeContext) {
        ed::SetCurrentEditor(m_nodeContext);
        ed::ClearSelection(); // ゴースト選択状態によるクラッシュを防ぐ
    }

    m_idCounter = state.value("last_id_counter", 1);

    // ノードの復元
    for (auto& jn : state["editor_nodes"]) {
        EditorBTNode node;
        node.id = jn["id"];
        node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
        node.name = jn.value("name", "Unknown");
        node.actionOrConditionId = static_cast<ActionID>(jn.value("actionId", 0));
        node.posX = jn.value("posX", 0.0f);
        node.posY = jn.value("posY", 0.0f);

        if (node.type == EditorBTNodeType::Group) {
            node.width = jn.value("width", 400.0f);
            node.height = jn.value("height", 400.0f);
        }
        if (node.type == EditorBTNodeType::Decorator) {
            node.decoratorType = jn.value("decoratorType", 0);
            node.decoratorParam = jn.value("decoratorParam", 0.0f);
        }

        node.inputPin = { jn.value("inputPinId", 0u), node.id, true };
        node.outputPin = { jn.value("outputPinId", 0u), node.id, false };

        // 座標をキャンバスに再適用させるため false にしておく
        node.positionInitialized = false;

        m_nodes.push_back(node);
    }

    // リンクの復元
    if (state.contains("editor_links")) {
        for (auto& jl : state["editor_links"]) {
            BTLink link;
            link.id = jl["id"];
            link.startPinId = jl["startPin"];
            link.endPinId = jl["endPin"];
            m_links.push_back(link);
        }
    }

    // =========================================================
    //  復元が終わったら、記憶していた選択状態を元に戻す
    // =========================================================
    if (m_nodeContext && !selectedNodes.empty()) {
        for (auto id : selectedNodes) {
            ed::SelectNode(id, true); // true = 複数選択のアペンド
        }
    }

    m_executionOrderDirty = true;
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
    m_nodes.push_back(node);
}

void BehaviorTreeWindow::DrawContents(EditorContext& context) {
    ed::SetCurrentEditor(m_nodeContext);

    // =========================================================================
    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    // =========================================================================
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    if (availableSize.x < 50.0f || availableSize.y < 50.0f)
    {
        // 次のフレームで安全な領域を確保してもらうために、ダミー（透明の箱）だけ置いて逃げる
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }


    if (ImGui::BeginTable("BTEditorTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, availableSize))
    {

        // 初期幅の比率を設定 (左のワークスペースが75%、右のインスペクタが25%)
        ImGui::TableSetupColumn("Workspace", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.25f);
        ImGui::TableNextRow();

        // --- 左ペイン (ツールバー ＋ ノードエディタ) ---
        ImGui::TableSetColumnIndex(0);
        DrawToolbar();
        DrawNodeWorkspace(context); // ※この中でGetContentRegionAvail()が安全に呼ばれるようになる

        // --- 右ペイン (インスペクタ) ---
        ImGui::TableSetColumnIndex(1);
        DrawInspector();

        ImGui::EndTable();
    }
}

// ====================================================================
// 描画：ツールバー（「日本語（英語名）」ボタン）
// ====================================================================
void BehaviorTreeWindow::DrawToolbar() {
    if (ImGui::Button("保存 ( Save )")) {
        char path[MAX_PATH] = "";
        auto picked         = FileDialog::SavePreset("BehaviorTreeNode", "Save Behavior Tree", "BossAI.json");
        if (picked)
        {
            std::string rel = FileDialog::ConvertToRelativePath(*picked);
            SaveGraph(rel);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("開く (Open...)")) {
        char path[MAX_PATH] = "";
        auto picked         = FileDialog::OpenPreset("BehaviorTreeNode", "Open Behavior Tree");
        if (picked)
        {
            std::string rel = FileDialog::ConvertToRelativePath(*picked);
            LoadGraph(rel);
        }
    }

    ImGui::Separator();

    // ノード追加ボタン
    //if (ImGui::Button("+ セレクター (Selector)")) 
    //    CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", 200, 0); 
    //ImGui::SameLine();
    //if (ImGui::Button("+ シーケンス (Sequence)")) 
    //    CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", 200, 50); 
    //ImGui::SameLine();
    //if (ImGui::Button("+ コンディション (Condition)")) 
    //    CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", 400, 0); 
    //ImGui::SameLine();
    //if (ImGui::Button("+ アクション (Action)")) 
    //    CreateNode(EditorBTNodeType::Action, "アクション (Action)", 400, 50);
    //ImGui::SameLine();
    //if (ImGui::Button("+ デコレーター (Decorator)"))
    //    CreateNode(EditorBTNodeType::Decorator, "デコレーター (Decorator)", 600, 0);

    //ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::Checkbox("自動追従 (Auto Follow)", &m_autoFollow);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("チェックを入れると、ゲーム実行中に現在アクティブなノードへカメラが自動で移動します。");
    }
}

/**
 * @brief 毎フレーム、親子関係から実行優先順位を計算する
 * @note DOD（データ指向設計）において、配列の並び順はキャッシュ効率に直結する重要な情報である
 */
void BehaviorTreeWindow::UpdateExecutionOrder() {
    // 全ての順番を一旦リセット
    for (auto& n : m_nodes) n.executionOrder = -1;

    // 1. 各親ノードに対して、接続されている子を取得
    for (auto& parent : m_nodes) {
        if (parent.outputPin.id == 0) continue;

        std::vector<EditorBTNode*> children;
        for (const auto& link : m_links) {
            if (link.startPinId == parent.outputPin.id) {
                for (auto& child : m_nodes) {
                    if (child.inputPin.id == link.endPinId) {
                        children.push_back(&child);
                    }
                }
            }
        }

        // 子ノードを X 座標順（左→右）に並べ、その順を実行順とする
        std::sort(children.begin(), children.end(), [](EditorBTNode* a, EditorBTNode* b) {
            return a->posX < b->posX;
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
void BehaviorTreeWindow::DrawNodeWorkspace(EditorContext& context) {

    // リンクやノードに変更があった時だけソート処理を走らせる
    if (m_executionOrderDirty) {
        UpdateExecutionOrder();
        m_executionOrderDirty = false;
    }

    // =========================================================================
    // 子ウィンドウ側でも同じ極小サイズの判定を行う
    // =========================================================================
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50.0f || avail.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    ed::SetCurrentEditor(m_nodeContext);

    // ImVec2(0.0f, 0.0f) をやめ、安全が保証されたサイズを明示的に渡す
    ed::Begin("My Node Editor");


    // =======================================================
    //  選択中のエンティティから、実行中のAI状態（デバッグ配列）を取得
    // =======================================================
    BehaviorTreeComponent* activeAI = nullptr;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world) {
        activeAI = context.world->GetComponent<BehaviorTreeComponent>(context.selectedEntity);
    }

    uint32_t currentActiveNodeId = 0;
    bool isActionNodeRunning = false;

    for (auto& node : m_nodes) {
        if (!node.positionInitialized) {
            ed::SetNodePosition(node.id, ImVec2(node.posX, node.posY));
            node.positionInitialized = true;
        }

        // =========================================================
        // グループ（コメント）ノードの専用描画 (安定版ベースのUE5装飾)
        // =========================================================
        if (node.type == EditorBTNodeType::Group) {

            // --- A. 選択状態による動的なハイライト（UE5風オレンジ発光） ---
            bool isSelected = ed::IsNodeSelected(node.id);
            ImColor borderColor = isSelected ? ImColor(255, 165, 0, 255) : ImColor(150, 150, 150, 80);
            ImColor bgColor = isSelected ? ImColor(40, 40, 40, 100) : ImColor(30, 30, 30, 60);

            ed::PushStyleColor(ed::StyleColor_GroupBg, bgColor);
            ed::PushStyleColor(ed::StyleColor_GroupBorder, borderColor);
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 8.0f);

            // BeginNode を先に呼ぶ
            ed::BeginNode(node.id);
            ImGui::PushID(node.id);

            // --- LOD: ズームに応じてフォントサイズを調整してラベルを描画 ---
            float currentZoom = ed::GetCurrentZoom();
            ImVec2 labelScreenPos = ImGui::GetCursorScreenPos();

            // ===============================================================
            // ヘッダーは Dummy で領域を取らず「絵」として描く。
            // ノード幅ぶんの Dummy を置くと、その幅がさらにノード幅へ反映されて
            // 毎フレーム膨張していくため、最小サイズに留める。
            // ===============================================================
            ImGui::Dummy(ImVec2(1.0f, 1.0f));

            ImDrawList* groupDrawList = ImGui::GetWindowDrawList();

            // --- B. 背景グラフィックとしてのヘッダー帯の描画 ---
            // ※ ダミーで領域を取るのではなく、文字の後ろに直接「絵」として描くことで膨張を防ぐ
            float headerHeight = 28.0f;
            ImVec2 headerMax = ImVec2(labelScreenPos.x + node.width, labelScreenPos.y + headerHeight);

            // ヘッダーの背景色と下部のライン
            groupDrawList->AddRectFilled(labelScreenPos, headerMax, ImColor(50, 50, 50, 180), 8.0f, ImDrawFlags_RoundCornersTop);
            groupDrawList->AddLine(ImVec2(labelScreenPos.x, headerMax.y), headerMax, ImColor(0, 0, 0, 150), 2.0f);

            if (currentZoom < 0.6f) {
                // ズームアウト時はフォントを拡大して視認性を保つ

                // zoom が 0 に近づくと除算結果が発散するため、下限と上限の両方で抑える
                float safeZoom = (std::max)(0.01f, currentZoom);
                float customFontSize = (std::min)(64.0f, ImGui::GetFontSize() / safeZoom * 0.8f);

                groupDrawList->AddText(
                    ImGui::GetFont(), customFontSize,
                    labelScreenPos, ImColor(220, 220, 220, 255),
                    node.name.c_str());
            }
            else {
                // 通常時のテキスト（少し右下にズラして綺麗に配置）
                ImVec2 textPos = ImVec2(labelScreenPos.x + 8, labelScreenPos.y + 6);
                groupDrawList->AddText(textPos, ImColor(220, 220, 220, 255), ("// " + node.name).c_str());
            }

            // ed::Group() がリサイズハンドルを自動描画・管理する
            ed::Group(ImVec2(node.width, node.height));

            ImGui::PopID();
            ed::EndNode();

            ed::PopStyleVar(1);
            ed::PopStyleColor(2);

            // EndNode 後にエディタが管理している実際のサイズを読み返す
            ImVec2 actualSize = ed::GetNodeSize(node.id);
            if (actualSize.x > 10.0f && actualSize.y > 10.0f) {
                // 最小サイズを下回らないようにガード
                node.width = (std::max)(100.0f, actualSize.x);
                node.height = (std::max)(50.0f, actualSize.y);
            }

            // 座標を更新して次のノードへ
            ImVec2 p = ed::GetNodePosition(node.id);
            node.posX = p.x;
            node.posY = p.y;
            continue;
        }

        // =======================================================
        // 1. スタイリング：ノードの種類に応じた色の決定（UE5準拠）
        // =======================================================
        ImColor headerColor;
        switch (node.type) {
        case EditorBTNodeType::Root:      headerColor = ImColor(40, 40, 40); break;      // 黒
        case EditorBTNodeType::Selector:  headerColor = ImColor(120, 30, 30); break;    // 深い赤
        case EditorBTNodeType::Sequence:  headerColor = ImColor(30, 80, 120); break;    // 深い青
        case EditorBTNodeType::Decorator: headerColor = ImColor(120, 100, 20); break;   // 金/黄
        case EditorBTNodeType::Condition: headerColor = ImColor(30, 120, 60); break;    // 緑
        case EditorBTNodeType::Action:    headerColor = ImColor(40, 100, 150); break;   // 明るい青
        case EditorBTNodeType::RandomSelector:
            headerColor = ImColor(180, 80, 180); // 紫色にして一目で「運要素」だと分かるようにする
            break;
        default:                          headerColor = ImColor(100, 100, 100); break;
        }

        // =======================================================
        //  リアルタイムデバッグ表示（状態による枠線の発光）
        // =======================================================
        ImColor borderColor = ImColor(60, 60, 60, 255); // 通常の枠線色
        float borderWidth = 1.0f;

        // activeAIが存在し、エディタノードがランタイム配列と紐付いている場合のみ状態を見る
        if (activeAI && node.runtimeFlatIndex >= 0 && node.runtimeFlatIndex < activeAI->debugNodeStates.size()) {
            BTDebugState state = activeAI->debugNodeStates[node.runtimeFlatIndex];

            if (state == BTDebugState::Running) {
                // 実行中（Running）は黄色く脈打つ
                float blink = (sinf(static_cast<float>(ImGui::GetTime()) * 15.0f) * 0.5f) + 0.5f;
                borderColor = ImColor(255, 200, 0, 200 + static_cast<int>(55 * blink));
                borderWidth = 3.0f;

                // 親ノード(Sequence等)もRunningになるが、実際に作業している末端の「Action」ノードを優先してカメラで追う
                if (!isActionNodeRunning || node.type == EditorBTNodeType::Action) {
                    currentActiveNodeId = node.id;
                    if (node.type == EditorBTNodeType::Action) {
                        isActionNodeRunning = true;
                    }
                }

            }
            else if (state == BTDebugState::Success) {
                // 成功（Success）は緑色
                borderColor = ImColor(50, 255, 50, 200);
                borderWidth = 2.0f;
            }
            else if (state == BTDebugState::Failure) {
                // 失敗（Failure）は赤色
                borderColor = ImColor(255, 50, 50, 200);
                borderWidth = 2.0f;
            }
        }

        // ノード全体の背景とボーダーを設定
        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(25, 25, 25, 240));
        ed::PushStyleColor(ed::StyleColor_NodeBorder, borderColor);
        ed::PushStyleVar(ed::StyleVar_NodeRounding, 6.0f);
        ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, borderWidth); // 枠線の太さを変更可能に

        // =========================================================
        // 2. ノード描画開始
        // =========================================================
        ed::BeginNode(node.id);

        const float NODE_WIDTH = 180.0f; // 幅を少し広くしてプロ感を出す
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // --- A. 上部入力バー（UE5スタイル） ---
        if (node.inputPin.id != 0) {
            ed::BeginPin(node.inputPin.id, ed::PinKind::Input);
            ImGui::Dummy(ImVec2(NODE_WIDTH, 20.0f)); // 高さを12→20に拡大し、掴みやすくした

            ImVec2 pMin = ImGui::GetItemRectMin();
            ImVec2 pMax = ImGui::GetItemRectMax();
            // 接続バーの描画
            drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 14), ImVec2(pMax.x - 2, pMax.y - 2), ImColor(180, 180, 180), 2.0f);
            ed::EndPin();
        }

        // --- B. ヘッダー（タイトル部分） ---
        ImGui::BeginGroup();
        // ヘッダー背景の描画
        ImVec2 headerStart = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(NODE_WIDTH, 32.0f));
        ImVec2 headerEnd = ImGui::GetItemRectMax();
        drawList->AddRectFilled(headerStart, headerEnd, headerColor, 4.0f, ImDrawFlags_RoundCornersTop);

        // タイトル文字の配置
        ImGui::SetCursorScreenPos(ImVec2(headerStart.x + 10, headerStart.y + 8));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // 必要なら太字フォント等に変更
        ImGui::Text("%s", node.name.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();

        // --- C. コンテンツ（詳細情報） ---
        ImGui::BeginGroup();
        ImGui::Indent(10.0f);
        ImGui::Spacing();

        if (node.executionOrder != -1 && node.type != EditorBTNodeType::Group) {
            ImGui::TextColored(ImVec4(1, 1, 0, 0.8f), "Priority: %d", node.executionOrder);
        }

        if (node.type == EditorBTNodeType::Action) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "> %s",
                GetActionDisplayName(node.actionOrConditionId));

            // Waitアクション時の残り時間リアルタイム表示
            if (activeAI && node.runtimeFlatIndex >= 0
                && node.runtimeFlatIndex < activeAI->nodeTimers.size()) {
                float currentTimer = activeAI->nodeTimers[node.runtimeFlatIndex];

                if (currentTimer > 0.0f && IsWaitAction(node.actionOrConditionId)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Wait: %.1fs", currentTimer);
                }
            }
        }
        else if (node.type == EditorBTNodeType::Condition) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "? %s",
                GetConditionDisplayName(node.actionOrConditionId));
        }
        // =======================================================
        //  デコレーターの表示とリアルタイム・プログレスバー
        // =======================================================
        else if (node.type == EditorBTNodeType::Decorator) {
            const char* decName = "Unknown";
            if (node.decoratorType == 0) decName = "Inverter (反転)";
            else if (node.decoratorType == 1) decName = "Cooldown (待機)";
            else if (node.decoratorType == 2) decName = "Retry (再試行)";

            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "* %s", decName);

            // デバッガー接続時（ゲーム実行中）のみの表示
            if (activeAI && node.runtimeFlatIndex >= 0 && node.runtimeFlatIndex < activeAI->nodeTimers.size()) {
                if (node.decoratorType == 1) { // Cooldownの場合
                    float currentTimer = activeAI->nodeTimers[node.runtimeFlatIndex];
                    if (currentTimer > 0.0f) {
                        // クールダウン中は赤文字で残り秒数を表示
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cooling: %.1fs", currentTimer);

                        // プログレスバー（ゲージ）の描画
                        float maxTime = (std::max)(0.001f, node.decoratorParam); // ゼロ除算防止
                        float fraction = 1.0f - (currentTimer / maxTime); // 0.0(空) 〜 1.0(満タン)

                        // プログレスバーの色を赤っぽくする
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        ImGui::ProgressBar(fraction, ImVec2(NODE_WIDTH - 20, 6), ""); // ゲージのみ表示
                        ImGui::PopStyleColor();
                    }
                    else {
                        // 発動可能な時は緑色で Ready と表示
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Ready");
                    }
                }
            }
        }

        ImGui::Unindent(10.0f);
        ImGui::Spacing();
        ImGui::EndGroup();



        // --- D. 下部出力バー（UE5スタイル） ---
        if (node.outputPin.id != 0) {
            ImGui::Spacing();
            ed::BeginPin(node.outputPin.id, ed::PinKind::Output);
            ImGui::Dummy(ImVec2(NODE_WIDTH, 20.0f)); // 高さを12→20に拡大

            ImVec2 pMin = ImGui::GetItemRectMin();
            ImVec2 pMax = ImGui::GetItemRectMax();
            // 接続バーの描画（少し明るい色にする）
            drawList->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 2), ImVec2(pMax.x - 2, pMax.y - 6), ImColor(100, 100, 100), 2.0f);
            ed::EndPin();
        }

        ed::EndNode();
        // PopStyleVarを1から2に変更（NodeBorderWidthを追加したため）
        ed::PopStyleVar(2);
        ed::PopStyleColor(2);

        // 座標更新
        ImVec2 p = ed::GetNodePosition(node.id);
        node.posX = p.x;
        node.posY = p.y;
    }


    // =========================================================
    // 血流の可視化（Execution Flow Debugging）
    // =========================================================
    for (auto& link : m_links) {
        // デフォルトの管の状態（非アクティブ）
        ImVec4 linkColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // 白
        float linkThickness = 2.0f; // 細い

        // 1. この管に血液を流し込んでいる「親ノード」を検索する
        auto parentNodeIt = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode& n) {
            return n.outputPin.id == link.startPinId;
            });

        if (parentNodeIt != m_nodes.end()) {
            // 2. ゲーム実行中(activeAI)かつBake済みのノードなら、ランタイムの配列から状態を取得する
            if (activeAI && parentNodeIt->runtimeFlatIndex >= 0 && parentNodeIt->runtimeFlatIndex < activeAI->debugNodeStates.size()) {

                // 正しい Enum である BTDebugState を取得
                BTDebugState state = activeAI->debugNodeStates[parentNodeIt->runtimeFlatIndex];

                // 3. 状態に応じた「血流エフェクト」の適用
                if (state == BTDebugState::Running) { // 実行中
                    float time = static_cast<float>(ImGui::GetTime());
                    float pulse = (std::sin(time * 10.0f) + 1.0f) * 0.5f;
                    float alpha = 0.5f + (pulse * 0.5f);

                    linkColor = ImVec4(1.0f, 0.6f, 0.0f, alpha);
                    linkThickness = 4.0f;
                }
                else if (state == BTDebugState::Success) { // 成功して通過
                    linkColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // 鮮やかな緑
                    linkThickness = 3.0f;
                }
                else if (state == BTDebugState::Failure) { // 失敗して遮断
                    linkColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // 鮮やかな赤
                    linkThickness = 3.0f;
                }
            }
        }

        // 4. 決定した色と太さでリンクを描画
        ed::Link(link.id, link.startPinId, link.endPinId, linkColor, linkThickness);
    }

    // =======================================================
    // 実行中ノードへのオートフォロー（追従中もユーザーの選択は保持する）
    // =======================================================
    if (m_autoFollow && currentActiveNodeId != 0) {
        if (m_lastTrackedNodeId != currentActiveNodeId) {

            // 1. 現在ユーザーが選択しているノード（インスペクタで見ているもの等）を記憶
            std::vector<ed::NodeId> selectedNodes;
            selectedNodes.resize(ed::GetSelectedObjectCount());
            int count = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
            selectedNodes.resize(count);

            // 2. 一瞬だけターゲットを単独選択する
            ed::ClearSelection();
            ed::SelectNode(currentActiveNodeId);

            // 3. 選択中のもの（＝ターゲット）へカメラを向ける命令を出す
            // false = ズームインしない（引き目を維持）, 0.4f = 0.4秒かけて滑らかに移動
            ed::NavigateToSelection(false, 0.4f);

            // 4. ユーザーの元の選択状態を即座に復元する（インスペクタの表示は切り替わらない）
            ed::ClearSelection();
            for (auto id : selectedNodes) {
                ed::SelectNode(id, true); // true = 追加選択
            }

            m_lastTrackedNodeId = currentActiveNodeId;
        }
    }
    else if (currentActiveNodeId == 0) {
        m_lastTrackedNodeId = 0; // 実行が止まったらリセット
    }


    if (ed::BeginCreate()) {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId)) {
            if (startPinId && endPinId) {

                // 1. ピンの属性（InputかOutputか）を特定する
                bool startIsInput = false;
                bool endIsInput = false;
                for (const auto& n : m_nodes) {
                    if (n.inputPin.id == startPinId.Get()) startIsInput = true;
                    if (n.inputPin.id == endPinId.Get()) endIsInput = true;
                }

                // 2. Input同士、Output同士の接続は赤線にして拒否する
                if (startIsInput == endIsInput) {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
                else {
                    if (ed::AcceptNewItem()) {
                        BTLink newLink;
                        newLink.id = GetNextId();

                        // 3. ユーザーのドラッグ方向に依存せず、必ず startPinId に「出力ピン(親)」、endPinId に「入力ピン(子)」を入れる
                        if (startIsInput) {
                            newLink.startPinId = ToEditorId(endPinId.Get());
                            newLink.endPinId   = ToEditorId(startPinId.Get());
                        }
                        else {
                            newLink.startPinId = ToEditorId(startPinId.Get());
                            newLink.endPinId   = ToEditorId(endPinId.Get());
                        }

                        m_links.push_back(newLink);
                        m_executionOrderDirty = true;
                    }
                }
            }
        }
    }
    ed::EndCreate();

    // =======================================================
     //  プロフェッショナル向け便利ショートカット機能
     // =======================================================
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        // 2. Ctrl + D キーで選択ノードを瞬時に複製 (Duplicate) のみを残す
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
            DuplicateSelectedNodes();
        }
    }


    bool itemsDeleted = false;
    if (ed::BeginDelete()) {
        ed::NodeId deletedNodeId = 0;
        while (ed::QueryDeletedNode(&deletedNodeId)) {
            if (ed::AcceptDeletedItem()) {
                DeleteNodeAndLinks(ToEditorId(deletedNodeId.Get()));
                itemsDeleted = true;
            }
        }
        ed::LinkId deletedLinkId = 0;
        while (ed::QueryDeletedLink(&deletedLinkId)) {
            if (ed::AcceptDeletedItem()) {
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                    [&](const BTLink& l) { return l.id == deletedLinkId.Get(); }), m_links.end());
                m_executionOrderDirty = true;
                itemsDeleted = true;
            }
        }
    }
    ed::EndDelete();

    // 削除が完了したらUndoスタックに記録する
    if (itemsDeleted) {
        RecordStateForUndo("Delete Node/Link");
    }

    HandleContextMenu();

    // 初回のズーム合わせは ed::End() より前で行う。ノードサイズが確定するまで数フレーム待つ。
    if (m_needInitialZoom) {
        m_framesSinceOpen++;

        // ImGuiがノードの幅と高さを完全に計算し終わるまで待つ（3フレーム目以降）
        if (m_framesSinceOpen > 2) {
            
            // NavigateToContent 呼び出し時のフリーズ・NaN 対策
            // 1. ノードが無い状態で NavigateToContent を呼ぶとゼロ除算で死ぬため回避
            // 2. 引数に 0.0f を渡して「アニメーション無し(即時反映)」にし、カメラ補間計算中の NaN 増殖を封じる
            if (!m_nodes.empty()) {
                ed::NavigateToContent(0.0f);
            }
            
            m_needInitialZoom = false;
        }
    }

    ed::End();
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
        uint32_t selectedId = ToEditorId(selectedNodes[0].Get());
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode& n) { return n.id == selectedId; });

        if (it != m_nodes.end()) {
            // =========================================================
            // Undoを安全に記録するための遅延フラグ
            // =========================================================
            bool needsUndo = false;
            std::string undoMessage = "Inspector Changed";


            // 機能追加: ノード名の変更（コメントや整理に必須）
            char nameBuf[256];
            strcpy_s(nameBuf, it->name.c_str());
            if (ImGui::InputText("名前 (Name)", nameBuf, sizeof(nameBuf))) {
                it->name = nameBuf;
            }

            // 入力が完了（エンターかフォーカス外れ）した時だけUndo記録
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                needsUndo = true;
                undoMessage = "Change Node Name";
            }

            // =========================================================
            // 最高のUXを実現する「ノードの取扱説明書」UI
            // =========================================================
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f)); // 黄色テキスト
            ImGui::Text("[ノードの役割と使い方]");
            ImGui::PopStyleColor();

            // 自動折り返しを有効にして、ウィンドウ幅に合わせてテキストを改行させる
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);

            switch (it->type) {
            case EditorBTNodeType::Selector:
                ImGui::Text("【Selector (セレクター / OR条件)】\n"
                    "子ノードを左から順に実行し、どれか1つでも「Success(成功)」すれば親にSuccessを返して終了します。\n"
                    "💡 使い方：優先度の高い行動（左）から順番に試す「行動の分岐点」として使います。");
                break;
            case EditorBTNodeType::Sequence:
                ImGui::Text("【Sequence (シーケンス / AND条件)】\n"
                    "子ノードを左から順に実行し、1つでも「Failure(失敗)」したら親にFailureを返して終了します。\n"
                    "💡 使い方：「条件チェック」→「近づく」→「攻撃する」といった、一連のアクションを順番にこなすセットを作るのに使います。");
                break;
            case EditorBTNodeType::Condition:
                ImGui::Text("【Condition (条件)】\n"
                    "状況（距離やHPなど）を判定し、SuccessかFailureを即座に返します。\n"
                    "💡 使い方：Sequenceの先頭（一番左）に置いて、その行動セットを実行してよいかの「門番」として使います。");
                break;
            case EditorBTNodeType::Action:
                ImGui::Text("【Action (アクション / 行動)】\n"
                    "実際にAIを動かす末端の命令です。ゲームのロジックに命令を送ります。\n"
                    "💡 使い方：移動や攻撃など。実行に時間がかかる行動は「Running(実行中)」を返し続け、次フレームも呼ばれます。\n"
                    "📌 現状のBoss実行層が対応するのは「移動(Move)」のみ。ドローン系は陣形指示、Waitはタイマー待機です。");
                break;
            case EditorBTNodeType::Decorator:
                ImGui::Text("【Decorator (デコレーター / 修飾)】\n"
                    "下に1つだけ子ノードを持ち、その子の結果や実行タイミングを操作します。\n"
                    "💡 使い方：強力な攻撃に「10秒のクールダウン」をつけたり、失敗時に「2回リトライ」させたりする制限をかけるのに使います。");
                break;
            case EditorBTNodeType::Group:
                ImGui::Text("【Group (グループ / コメント)】\n"
                    "複数のノードを視覚的に囲んで整理するための枠です。\n"
                    "💡 使い方：AIの実行ロジックには一切影響を与えません。メモや整理に使ってください。");
                break;
            case EditorBTNodeType::RandomSelector:
                ImGui::Text("【Random Selector (ランダムセレクター)】\n"
                    "子ノードの中から毎回「ランダムな順番」で実行を試み、1つでも成功すれば終了します。\n"
                    "💡 使い方：敵の近接攻撃の3択など、プレイヤーに次の行動を予測させない「揺さぶり（Mix-up）」に最適です。");
                break;
            }
            ImGui::PopTextWrapPos(); // 自動折り返し終了
            ImGui::Separator();

            // ---------------------------------------------------------
            // 各種プロパティの編集とUndoフラグのセット
            // ---------------------------------------------------------
            if (it->type == EditorBTNodeType::Condition) {
                ImGui::Separator();
                ImGui::Text("条件設定 (Condition):");
                if (DrawConditionCombo("条件を選ぶ", it->actionOrConditionId)) {
                    m_executionOrderDirty = true;
                    needsUndo = true;
                    undoMessage = "Change Condition";
                }
            }
            else if (it->type == EditorBTNodeType::Action) {
                ImGui::Separator();
                ImGui::Text("行動設定 (Action):");
                if (DrawActionCombo("行動を選ぶ", it->actionOrConditionId)) {
                    m_executionOrderDirty = true;
                    needsUndo = true;
                    undoMessage = "Change Action";
                }

                // 選択中アクションの補足（最小構成 BossAction との対応）
                if (IsBossPhysicalAction(it->actionOrConditionId)) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                        "→ BossActionSystem が物理移動を実行");
                }
                else if (IsDroneAction(it->actionOrConditionId)) {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                        "→ BossCommand.requestFormation に陣形を指示");
                }
                else if (IsWaitAction(it->actionOrConditionId)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                        "→ nodeTimers で待機（BossAction 非依存）");
                }
                else if (it->actionOrConditionId == CCL::AI::A_ResetDamage) {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                        "→ ブラックボードの蓄積ダメージをリセット");
                }
                else if (!FindActionEntry(it->actionOrConditionId)) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                        "⚠ 未登録ID（旧BTからの互換データの可能性）");
                }
            }

            if (it->type == EditorBTNodeType::Decorator) {
                ImGui::Separator();
                ImGui::Text("デコレーター設定 (Decorator):");

                const char* decNames[] = { "0: 結果反転 (Inverter)", "1: クールダウン (Cooldown)", "2: リトライ (Retry)", "3: 非同期 (Async)" };
                if (ImGui::Combo("タイプ", &it->decoratorType, decNames, IM_ARRAYSIZE(decNames))) {
                    m_executionOrderDirty = true;
                    needsUndo = true;
                    undoMessage = "Change Decorator Type";
                }

                if (it->decoratorType == 1) { // Cooldown
                    // ドラッグ中は数値を変えるだけ
                    ImGui::DragFloat("待機時間 (秒)", &it->decoratorParam, 0.1f, 0.0f, 60.0f);

                    // マウスを離した時だけUndo記録
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        needsUndo = true;
                        undoMessage = "Change Cooldown Time";
                    }
                }
                else if (it->decoratorType == 2) { // Retry
                    int retryCount = static_cast<int>(it->decoratorParam);
                    if (ImGui::DragInt("リトライ回数", &retryCount, 1, 1, 10)) {
                        it->decoratorParam = static_cast<float>(retryCount);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        needsUndo = true;
                        undoMessage = "Change Retry Count";
                    }
                }
            }


            // =========================================================
            // すべての `it` の使用が終わった「一番最後」にUndoを記録する！
            // これによりイテレータ無効化によるクラッシュを完全に防ぐ
            // =========================================================
            if (needsUndo) {
                RecordStateForUndo(undoMessage);
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
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode& n) { return n.id == nodeId; });
    if (it == m_nodes.end()) return;

    // 2. そのノードが持っているピンのIDを取得
    uint32_t inPinId = it->inputPin.id;
    uint32_t outPinId = it->outputPin.id;

    // 3. このピンに繋がっているリンクを全て配列から消し去る
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
        [inPinId, outPinId](const BTLink& link) {
            return link.startPinId == outPinId || link.endPinId == inPinId;
        }), m_links.end());

    // 4. 最後にノード本体を消す
    m_nodes.erase(it);

    m_executionOrderDirty = true;

    CCL_LOG_INFO(LogCategory::Editor, "Node(ID:%u) and its connected links were deleted.", nodeId);
}

// ====================================================================
// 右クリックコンテキストメニュー処理
// ====================================================================
void BehaviorTreeWindow::HandleContextMenu() {
    ed::Suspend();

    // =======================================================
    //  Spaceキーでマウスカーソルの位置にメニューを出す
    // =======================================================
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePos(); // Spaceを押した瞬間のマウス座標を記憶
    }

    if (ed::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("Create New Node")) {
        ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);

        if (ImGui::MenuItem("セレクター (Selector)")) CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("シーケンス (Sequence)")) CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("ランダムセレクター（Random Selector）")) {
            CreateNode(EditorBTNodeType::RandomSelector, "ランダムセレクター（Random Selector）", canvasPos.x, canvasPos.y);
        }
        ImGui::Separator();

        if (ImGui::MenuItem("デコレーター (Decorator)")) CreateNode(EditorBTNodeType::Decorator, "デコレーター (Decorator)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("コンディション (Condition)")) CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("アクション (Action)")) CreateNode(EditorBTNodeType::Action, "アクション (Action)", canvasPos.x, canvasPos.y);
        ImGui::Separator();
        // グループノード生成
        if (ImGui::MenuItem("コメントグループ (Group)")) CreateNode(EditorBTNodeType::Group, "コメント (Comment)", canvasPos.x, canvasPos.y);

        ImGui::Separator();
        // 複数ノードの複製
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
    ed::SetCurrentEditor(m_nodeContext);

    // ノード情報の保存
    json jNodes = json::array();
    for (auto& node : m_nodes) {
        json jn;
        jn["id"] = node.id;
        jn["type"] = static_cast<int>(node.type);
        jn["name"] = node.name;
        jn["actionId"] = static_cast<int>(node.actionOrConditionId);

        // ピンのIDを保存。これがリンクの復元に不可欠。
        jn["inputPinId"] = node.inputPin.id;
        jn["outputPinId"] = node.outputPin.id;

        ImVec2 pos = ed::GetNodePosition(node.id);
        jn["posX"] = pos.x;
        jn["posY"] = pos.y;
        // グループサイズを保存
        if (node.type == EditorBTNodeType::Group) {
            jn["width"] = node.width;
            jn["height"] = node.height;
        }
        // デコレーターのパラメータを保存する
        if (node.type == EditorBTNodeType::Decorator) {
            jn["decoratorType"] = node.decoratorType;
            jn["decoratorParam"] = node.decoratorParam;
        }

        jNodes.push_back(jn);
    }
    root["editor_nodes"] = jNodes;

    // リンク情報の保存
    json jLinks = json::array();
    for (auto& link : m_links) {
        json jl;
        jl["id"] = link.id;
        jl["startPin"] = link.startPinId;
        jl["endPin"] = link.endPinId;
        jLinks.push_back(jl);
    }
    root["editor_links"] = jLinks;
    root["last_id_counter"] = m_idCounter;

    // =======================================================
    //  ここで BuildBakedNodes() を呼び出して統合する
    // =======================================================
    root["nodes"] = BuildBakedNodes();
    root["assetId"] = 1;

    // 書き出し（1回だけファイルを開く）
    // ファイル書き込みは FileSystem 経由で行う
    std::string jsonStr = root.dump(4);
    if (FileSystem::WriteAllText(path, jsonStr)) {
        CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフを保存＆Bakeしました: %s", path.c_str());
    }

}

// =========================================================
// セーブ処理の統合 (上書きバグ解消 / 新children配列対応版)
// =========================================================
nlohmann::json BehaviorTreeWindow::BuildBakedNodes() {
    nlohmann::json jsonNodes = nlohmann::json::array();
    if (m_nodes.empty()) return jsonNodes;

    // 1. Rootノードを探す
    EditorBTNode* rootNode = nullptr;
    for (auto& n : m_nodes) {
        if (n.type == EditorBTNodeType::Root) { rootNode = &n; break; }
    }
    if (!rootNode) return jsonNodes;

    // ヘルパー：特定の出力ピンから繋がっている子ノードを「X座標順」に取得する
    auto getSortedChildren = [&](uint32_t outputPinId) {
        std::vector<EditorBTNode*> children;
        for (const auto& link : m_links) {
            if (link.startPinId == outputPinId) {
                for (auto& n : m_nodes) {
                    if (n.inputPin.id == link.endPinId) {
                        children.push_back(&n); break;
                    }
                }
            }
        }
        std::sort(children.begin(), children.end(), [](EditorBTNode* a, EditorBTNode* b) {
            return a->posX < b->posX;
            });
        return children;
        };

    auto rootChildren = getSortedChildren(rootNode->outputPin.id);
    if (rootChildren.empty()) return jsonNodes;

    // 2. BFS（幅優先探索）によるDODフラット配列の構築
    struct BakeTask { EditorBTNode* editorNode; int flatIndex; };
    std::queue<BakeTask> tasks;

    auto getRuntimeTypeString = [](EditorBTNodeType type) {
        switch (type) {
        case EditorBTNodeType::Selector:  return "Selector";
        case EditorBTNodeType::Sequence:  return "Sequence";
        case EditorBTNodeType::Condition: return "Condition";
        case EditorBTNodeType::Action:    return "Action";
        case EditorBTNodeType::Decorator: return "Decorator";
        case EditorBTNodeType::RandomSelector: return "RandomSelector";

        default: return "Unknown";
        }
        };

    // =======================================================
    // ユーザーがROOTに複数繋いでしまった場合の警告
    // =======================================================
    if (rootChildren.size() > 1) {
        CCL_LOG_ERROR(LogCategory::Editor, "【BTエラー】ROOTノードには複数の線を繋げません！必ず1つの「Selector」等に繋いでから分岐させてください。");
    }

    EditorBTNode* actualRoot = rootChildren[0];

    nlohmann::json rootJson;
    rootJson["type"] = getRuntimeTypeString(actualRoot->type);
    rootJson["actionOrConditionId"] = static_cast<int>(actualRoot->actionOrConditionId);
    rootJson["children"] = nlohmann::json::array(); // 配列として初期化
    jsonNodes.push_back(rootJson);

    tasks.push({ actualRoot, 0 });

    while (!tasks.empty()) {
        BakeTask task = tasks.front();
        tasks.pop();

        auto children = getSortedChildren(task.editorNode->outputPin.id);
        if (children.empty()) continue;

        // 子供たちを配列に追加し、次のキューへ
        for (EditorBTNode* child : children) {
            nlohmann::json childJson;
            childJson["type"] = getRuntimeTypeString(child->type);
            childJson["actionOrConditionId"] = static_cast<int>(child->actionOrConditionId);
            childJson["children"] = nlohmann::json::array(); // 配列として初期化

            // デコレーター情報のBake
            if (child->type == EditorBTNodeType::Decorator) {
                childJson["decoratorType"] = child->decoratorType;
                childJson["decoratorParam"] = child->decoratorParam;
            }

            child->runtimeFlatIndex = static_cast<int>(jsonNodes.size());

            // 親ノードの "children" 配列に、この子のインデックスを追加する
            jsonNodes[task.flatIndex]["children"].push_back(jsonNodes.size());

            jsonNodes.push_back(childJson);
            tasks.push({ child, static_cast<int>(jsonNodes.size() - 1) });
        }
    }

    return jsonNodes;
}



// ====================================================================
// ロード処理：ピンIDを生成せず、保存された値で復元する
// ====================================================================
void BehaviorTreeWindow::LoadGraph(const std::string& path) {
    // ファイル読み込みは FileSystem 経由で行う
    std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty()) return;

    json j;
    try {
        j = json::parse(jsonStr);
    }
    catch (...) {
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフのパースに失敗しました: %s", path.c_str());
        return;
    }

    m_nodes.clear();
    m_links.clear();
    m_idCounter = j.value("last_id_counter", 1);

    // ノードの復元
    for (auto& jn : j["editor_nodes"]) {
        EditorBTNode node;
        node.id = jn["id"];
        node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
        node.name = jn["name"];
        node.actionOrConditionId = static_cast<ActionID>(jn["actionId"].get<int>());
        node.posX = jn["posX"];
        node.posY = jn["posY"];
        // グループサイズを復元
        if (node.type == EditorBTNodeType::Group) {
            node.width = jn.value("width", 300.0f);
            node.height = jn.value("height", 200.0f);
        }

        // =======================================================
        // デコレーターのパラメータを復元する
        // =======================================================
        if (node.type == EditorBTNodeType::Decorator) {
            node.decoratorType = jn.value("decoratorType", 0);
            node.decoratorParam = jn.value("decoratorParam", 0.0f);
        }

        // GetNextId() を使わず、保存されたIDをそのままセットする
        node.inputPin = { jn.value("inputPinId", 0u), node.id, true };
        node.outputPin = { jn.value("outputPinId", 0u), node.id, false };

        m_nodes.push_back(node);
    }

    // リンクの復元
    for (auto& jl : j["editor_links"]) {
        BTLink link;
        link.id = jl["id"];
        link.startPinId = jl["startPin"];
        link.endPinId = jl["endPin"];
        m_links.push_back(link);
    }

    // =======================================================
    // ロード完了直後に「空回し」のBakeを実行する！
    // これにより、わざわざ保存しなくても `runtimeFlatIndex` が計算され、
    // ロードした瞬間からリアルタイムデバッグが可能になる。
    // =======================================================
    BuildBakedNodes();

    m_needInitialZoom = true;
    CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフをロードしました: %s", path.c_str());
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
        uint32_t oldId = ToEditorId(selectedNodeIds[i].Get());
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [oldId](const EditorBTNode& n) { return n.id == oldId; });

        if (it != m_nodes.end()) {
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
    for (const auto& link : m_links) {
        if (oldToNewPinId.count(link.startPinId) && oldToNewPinId.count(link.endPinId)) {
            BTLink newLink;
            newLink.id = GetNextId();
            newLink.startPinId = oldToNewPinId[link.startPinId];
            newLink.endPinId = oldToNewPinId[link.endPinId];
            newLinks.push_back(newLink);
        }
    }

    // 3. コピーした要素を世界に反映
    m_nodes.insert(m_nodes.end(), newNodes.begin(), newNodes.end());
    m_links.insert(m_links.end(), newLinks.begin(), newLinks.end());

    // 操作完了後、選択状態を解除する
    ed::ClearSelection();
    CCL_LOG_INFO(LogCategory::Editor, "%d nodes duplicated.", nodeCount);
}

