// BehaviorTreeWindow の本体：ウィンドウ枠・ツールバー・編集操作・Undo・表示名。
// 各ペインとグラフ処理は BehaviorTreeWindow_*.cpp に分けてある（一覧は BehaviorTreeWindow.h の冒頭）。
#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

using namespace BTEditor;

/// グラフの変更1回分。前後の状態を丸ごと持ち、Undo/Redo で差し替える
class BTGraphChangeCommand : public IEditorCommand
{
    BehaviorTreeWindow *m_window;   // 戻す先のウィンドウ
    nlohmann::json      m_oldState; // 変更前
    nlohmann::json      m_newState; // 変更後
    std::string         m_name;     // 履歴に出す操作名

public:
    BTGraphChangeCommand(BehaviorTreeWindow *window, const std::string &name, const nlohmann::json &oldState, const nlohmann::json &newState)
        : m_window(window), m_oldState(oldState), m_newState(newState), m_name(name)
    {
    }

    void Execute() override
    {
        m_window->RestoreGraphState(m_newState);
        CCL_LOG_INFO(LogCategory::Editor, "[BT Editor] Redo: %s", m_name.c_str());
    }
    void Undo() override
    {
        m_window->RestoreGraphState(m_oldState);
        CCL_LOG_INFO(LogCategory::Editor, "[BT Editor] Undo: %s", m_name.c_str());
    }
};

BehaviorTreeWindow::BehaviorTreeWindow() : EditorWindow("Behavior Tree Editor")
{
    // 視点や座標を ini に保存しない。前回クラッシュ時の壊れた座標が残ると復帰できないため。
    ed::Config config;
    config.SettingsFile = nullptr;
    m_nodeContext       = ed::CreateEditor(&config);

    CreateNode(EditorBTNodeType::Root, "ROOT", 0.0f, 0.0f);
}

BehaviorTreeWindow::~BehaviorTreeWindow()
{
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}

void BehaviorTreeWindow::RecordStateForUndo(const std::string &commandName)
{
    nlohmann::json newState = GetGraphStateAsJson();
    // 状態が変わっていればコマンドをスタックに積む
    if (m_lastSavedState != newState)
    {
        m_dirty = true; // グラフ変更はすべてここを通る
        EditorCommandHistory::Instance().ExecuteCommand(
            std::make_unique<BTGraphChangeCommand>(this, commandName, m_lastSavedState, newState));
        m_lastSavedState = newState;
    }
}

// ====================================================================
// ウィンドウ枠
// ====================================================================
void BehaviorTreeWindow::DrawContents(EditorContext &context)
{
    ed::SetCurrentEditor(m_nodeContext);

    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    if (availableSize.x < 50.0f || availableSize.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    // 親子関係と「畳まれて隠れているノード」を、どのペインより先に確定させる。
    //   アウトライン・キャンバス・インスペクタが同じフレームで同じ見え方を前提にできる。
    RebuildHierarchy();

    // 隠れたノードの選択は外す。見えないものがインスペクタに出続けたり、
    // Delete キーで見えないまま消えたりするのを防ぐ。
    for (uint32_t id : m_hiddenNodes)
    {
        if (ed::IsNodeSelected(id)) ed::DeselectNode(id);
    }

    const BehaviorTreeComponent *activeAI = FindActiveAI(context);

    if (ImGui::BeginTable("BTEditorTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, availableSize))
    {
        // 初期幅の比率（アウトライン18% / ワークスペース60% / インスペクタ22%）
        ImGui::TableSetupColumn("Outline", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Workspace", ImGuiTableColumnFlags_WidthStretch, 0.60f);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableNextRow();

        // --- 左ペイン (アウトライン) ---
        ImGui::TableSetColumnIndex(0);
        DrawOutliner(activeAI);

        // --- 中央ペイン (ツールバー ＋ ノードエディタ) ---
        ImGui::TableSetColumnIndex(1);
        DrawToolbar();
        DrawNodeWorkspace(activeAI);

        // --- 右ペイン (インスペクタ) ---
        ImGui::TableSetColumnIndex(2);
        DrawInspector();

        ImGui::EndTable();
    }
}

void BehaviorTreeWindow::DrawToolbar()
{
    if (ImGui::Button("保存 ( Save )"))
    {
        auto picked = FileDialog::SavePreset("BehaviorTreeNode", "Save Behavior Tree", "BossAI.json");
        if (picked)
        {
            std::string rel = FileDialog::ConvertToRelativePath(*picked);
            SaveGraph(rel);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("開く (Open...)"))
    {
        auto picked = FileDialog::OpenPreset("BehaviorTreeNode", "Open Behavior Tree");
        if (picked)
        {
            std::string rel = FileDialog::ConvertToRelativePath(*picked);
            // 未保存の変更があるなら、いきなり捨てずに確認する。
            //   ここには Undo で戻す手段が無い（読み込んだ時点で履歴の基準も引き直す）。
            if (m_dirty)
                m_pendingOpenPath = rel;
            else
                LoadGraph(rel);
        }
    }

    // 未保存の表示。ウィンドウ名に "*" を足す手もあるが、ImGui はウィンドウ名を
    //   ID に使っているため、名前を変えるとドッキング配置が別物として扱われる。
    //   だからツールバー側に出す。
    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "● 未保存");
    }

    // 未保存のまま「開く」を押したときの確認。
    if (!m_pendingOpenPath.empty()) ImGui::OpenPopup("BTOpenConfirm");
    if (ImGui::BeginPopupModal("BTOpenConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("保存していない変更があります。");
        ImGui::TextDisabled("%s", m_pendingOpenPath.c_str());
        ImGui::Separator();
        if (ImGui::Button("破棄して開く", ImVec2(140, 0)))
        {
            const std::string path = m_pendingOpenPath;
            m_pendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
            LoadGraph(path);
        }
        ImGui::SameLine();
        if (ImGui::Button("やめる", ImVec2(140, 0)))
        {
            m_pendingOpenPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (ImGui::Button("整列 (Auto Layout)"))
        m_layoutRequested = true; // 実行はキャンバス描画の頭まで持ち越す
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("木の形に合わせて全ノードを並べ直します。\n"
                          "子の左右の並び順は保たれるので、実行順は変わりません。\n"
                          "コメント枠も、囲んでいたノードに合わせて追従します。");
    }

    ImGui::SameLine();
    if (ImGui::Button("選択を囲む (Ctrl+G)"))
        GroupSelectedNodes();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("選択中のノードをちょうど囲むコメント枠を作ります。");
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::Checkbox("自動追従 (Auto Follow)", &m_autoFollow);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("チェックを入れると、ゲーム実行中に現在アクティブなノードへカメラが自動で移動します。");
    }
}

// ====================================================================
// 編集操作
// ====================================================================
void BehaviorTreeWindow::CreateNode(EditorBTNodeType type, const std::string &name, float x, float y)
{
    EditorBTNode node;
    node.id   = GetNextId();
    node.type = type;
    node.name = UsesCustomName(type) ? name : ""; // 中身から名前を作る種類は名前を持たない

    // グループとルート以外は入力ピンを持つ
    if (type != EditorBTNodeType::Root && type != EditorBTNodeType::Group)
    {
        node.inputPin = {GetNextId(), node.id, true};
    }
    else
    {
        node.inputPin.id = 0;
    }

    // アクション/条件/グループ 以外は出力ピンを持つ
    if (type != EditorBTNodeType::Condition && type != EditorBTNodeType::Action && type != EditorBTNodeType::Group)
    {
        node.outputPin = {GetNextId(), node.id, false};
    }
    else
    {
        node.outputPin.id = 0;
    }

    node.posX                = x;
    node.posY                = y;
    node.positionInitialized = false;
    m_nodes.push_back(node);
}

void BehaviorTreeWindow::DeleteNodeAndLinks(uint32_t nodeId)
{
    // 1. 削除対象のノードを探す
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode &n) { return n.id == nodeId; });
    if (it == m_nodes.end()) return;

    // 2. そのノードが持っているピンのIDを取得
    uint32_t inPinId  = it->inputPin.id;
    uint32_t outPinId = it->outputPin.id;

    // 畳んだノードを消すと、隠れていた子孫が親の位置に重なったまま現れる。並べ直して見えるようにする。
    if (it->collapsed && HasChildren(nodeId)) m_layoutRequested = true;

    // 3. このピンに繋がっているリンクを全て配列から消し去る
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [inPinId, outPinId](const BTLink &link) {
                                     return link.startPinId == outPinId || link.endPinId == inPinId;
                                 }),
                  m_links.end());

    // 4. 最後にノード本体を消す
    m_nodes.erase(it);


    CCL_LOG_INFO(LogCategory::Editor, "Node(ID:%u) and its connected links were deleted.", nodeId);
}

void BehaviorTreeWindow::DuplicateSelectedNodes()
{
    std::vector<ed::NodeId> selectedNodeIds;
    selectedNodeIds.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodeIds.data(), static_cast<int>(selectedNodeIds.size()));

    if (nodeCount == 0) return;

    // 古いIDから新しいIDへのマッピングを記録（リンクの再接続に使う）
    std::map<uint32_t, uint32_t> oldToNewPinId;

    std::vector<EditorBTNode> newNodes;

    // 1. ノードの複製
    for (int i = 0; i < nodeCount; ++i)
    {
        uint32_t oldId = ToEditorId(selectedNodeIds[i].Get());
        auto     it    = std::find_if(m_nodes.begin(), m_nodes.end(), [oldId](const EditorBTNode &n) { return n.id == oldId; });

        if (it != m_nodes.end())
        {
            EditorBTNode newNode = *it;
            newNode.id           = GetNextId();
            newNode.posX += 50.0f; // 重ならないように右下に少しずらす
            newNode.posY += 50.0f;
            newNode.positionInitialized = false;
            newNode.executionOrder      = -1;
            // 畳みは外す。隠れている子孫は選択できないので複製されず、
            //   畳んだまま複製すると、後から繋いだ子が繋いだ瞬間に消えて見える。
            newNode.collapsed = false;

            // 新しいピンIDの割り当てとマッピング
            if (newNode.inputPin.id != 0)
            {
                uint32_t newIn                     = GetNextId();
                oldToNewPinId[newNode.inputPin.id] = newIn;
                newNode.inputPin.id                = newIn;
                newNode.inputPin.nodeId            = newNode.id;
            }
            if (newNode.outputPin.id != 0)
            {
                uint32_t newOut                     = GetNextId();
                oldToNewPinId[newNode.outputPin.id] = newOut;
                newNode.outputPin.id                = newOut;
                newNode.outputPin.nodeId            = newNode.id;
            }

            newNodes.push_back(newNode);
        }
    }

    // 2. リンクの複製（複製されたノード同士が繋がっていた場合のみ）
    std::vector<BTLink> newLinks;
    for (const auto &link : m_links)
    {
        if (oldToNewPinId.count(link.startPinId) && oldToNewPinId.count(link.endPinId))
        {
            BTLink newLink;
            newLink.id         = GetNextId();
            newLink.startPinId = oldToNewPinId[link.startPinId];
            newLink.endPinId   = oldToNewPinId[link.endPinId];
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

EditorBTNode *BehaviorTreeWindow::FindNode(uint32_t nodeId)
{
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode &n) { return n.id == nodeId; });
    return (it == m_nodes.end()) ? nullptr : &*it;
}

// ====================================================================
// 表示名・色・実行中の状態
// ====================================================================
const BehaviorTreeComponent *BehaviorTreeWindow::FindActiveAI(EditorContext &context)
{
    if (context.selectedEntity == CCL::ECS::InvalidEntityID || !context.world) return nullptr;
    return context.world->GetComponent<BehaviorTreeComponent>(context.selectedEntity);
}

ImColor BehaviorTreeWindow::HeaderColorOf(EditorBTNodeType type)
{
    // 8種類の色相を 40〜50° ずつ離して、似た色が隣り合わないようにしてある。
    // 明るさは、暗い背景の上で色の差が出て、かつ白い文字が読める程度にそろえている。
    switch (type)
    {
    case EditorBTNodeType::Root: return ImColor(90, 90, 96);              // 灰：始点（色を持たない）
    case EditorBTNodeType::Selector: return ImColor(180, 50, 50);         // 赤（色相 0°）
    case EditorBTNodeType::WeightedSelector: return ImColor(190, 70, 140); // ピンク（325°）：赤の仲間＝Selector の一種
    case EditorBTNodeType::Action: return ImColor(120, 70, 195);          // 紫（265°）：一番数が多いので他と被らない色。UE5 の Task と同じ
    case EditorBTNodeType::Sequence: return ImColor(40, 95, 195);         // 青（220°）
    case EditorBTNodeType::Parallel: return ImColor(20, 145, 150);        // 青緑（180°）
    case EditorBTNodeType::Condition: return ImColor(50, 150, 70);        // 緑（130°）
    case EditorBTNodeType::Decorator: return ImColor(180, 140, 30);       // 金（45°）
    default: return ImColor(100, 100, 100);
    }
}

const char *BehaviorTreeWindow::DecoratorNameOf(int decoratorType)
{
    switch (decoratorType)
    {
    case 0: return "Inverter (反転)";
    case 1: return "Cooldown (待機)";
    case 2: return "Retry (再試行)";
    default: return "Unknown";
    }
}

bool BehaviorTreeWindow::UsesCustomName(EditorBTNodeType type)
{
    switch (type)
    {
    case EditorBTNodeType::Action:
    case EditorBTNodeType::Condition:
    case EditorBTNodeType::Decorator:
        return false;
    default:
        return true;
    }
}

std::string BehaviorTreeWindow::FormatParam(const char *label, float value)
{
    // %g は 1.50 → "1.5"、25.0 → "25" のように余計な 0 を付けない
    char number[32];
    snprintf(number, sizeof(number), "%g", value);

    // "長さ [秒]" → 名前 "長さ" と 単位 "秒" に分ける。[単位] が無ければ名前だけ
    const std::string text    = label;
    const size_t      bracket = text.find(" [");
    if (bracket == std::string::npos || text.back() != ']')
        return text + " " + number;

    const std::string name = text.substr(0, bracket);
    const std::string unit = text.substr(bracket + 2, text.size() - bracket - 3);
    return name + " " + number + unit;
}

std::string BehaviorTreeWindow::GetDisplayName(const EditorBTNode &node)
{
    // ----------------------------------------------------------------
    // まとまり（Selector など）とコメント枠：自分で付けた名前
    // ----------------------------------------------------------------
    if (UsesCustomName(node.type)) return node.name;

    char number[32];
    snprintf(number, sizeof(number), "%g", node.param);

    // ----------------------------------------------------------------
    // Condition：表示名の "N" を param の値に置き換える
    //   登録表の表示名は「距離が N m 未満」の形なので、値を入れるとそのまま読める文になる
    // ----------------------------------------------------------------
    if (node.type == EditorBTNodeType::Condition)
    {
        std::string name = GetConditionDisplayName(node.actionOrConditionId);

        // 単独の "N" を探す（前後が英字なら単語の一部なので飛ばす）
        auto isAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        for (size_t i = 0; i < name.size(); ++i)
        {
            const bool alone = name[i] == 'N' && (i == 0 || !isAlpha(name[i - 1])) &&
                               (i + 1 == name.size() || !isAlpha(name[i + 1]));
            if (alone)
                return name.replace(i, 1, number);
        }

        // "N" を含まない表示名でも param を使うなら、後ろに添える
        if (const char *label = GetConditionParamLabel(node.actionOrConditionId))
            return name + "（" + FormatParam(label, node.param) + "）";
        return name;
    }

    // ----------------------------------------------------------------
    // Action：行動名＋param（データの技は技の名前）
    // ----------------------------------------------------------------
    if (node.type == EditorBTNodeType::Action)
    {
        if (node.actionOrConditionId == A_DroneFormation)
            return GetDroneFormationLabel(node.param);

        // 末尾の英語の併記「(Move To Player)」は外す。一覧が横に長くなりすぎるため
        std::string  name  = GetActionDisplayName(node.actionOrConditionId);
        const size_t paren = name.rfind(" (");
        if (paren != std::string::npos && name.back() == ')')
            name.erase(paren);

        if (const char *label = GetActionParamLabel(node.actionOrConditionId))
            return name + "（" + FormatParam(label, node.param) + "）";
        return name;
    }

    // ----------------------------------------------------------------
    // Decorator：種類＋param
    // ----------------------------------------------------------------
    switch (node.decoratorType)
    {
    case 1: return std::string("Cooldown ") + number + "秒";
    case 2: return std::string("Retry ") + number + "回";
    default: return DecoratorNameOf(node.decoratorType);
    }
}

BTDebugState BehaviorTreeWindow::DebugStateOf(const EditorBTNode &node, const BehaviorTreeComponent *activeAI)
{
    if (!activeAI || node.runtimeFlatIndex < 0 ||
        node.runtimeFlatIndex >= static_cast<int>(activeAI->debugNodeStates.size()))
        return BTDebugState::None;
    return activeAI->debugNodeStates[static_cast<size_t>(node.runtimeFlatIndex)];
}

std::optional<float> BehaviorTreeWindow::DebugTimerOf(const EditorBTNode &node, const BehaviorTreeComponent *activeAI)
{
    if (!activeAI || node.runtimeFlatIndex < 0 ||
        node.runtimeFlatIndex >= static_cast<int>(activeAI->nodeTimers.size()))
        return std::nullopt;
    return activeAI->nodeTimers[static_cast<size_t>(node.runtimeFlatIndex)];
}

const char *BehaviorTreeWindow::GetActionDisplayName(ActionID id)
{
    if (const ActionEntry *e = FindActionEntry(id))
        return e->name;
    static thread_local char buf[64];
    snprintf(buf, sizeof(buf), "不明なAction (ID:%d)", static_cast<int>(id));
    return buf;
}

const char *BehaviorTreeWindow::GetConditionDisplayName(ActionID id)
{
    if (const ActionEntry *e = FindConditionEntry(id))
        return e->name;
    static thread_local char buf[64];
    snprintf(buf, sizeof(buf), "不明なCondition (ID:%d)", static_cast<int>(id));
    return buf;
}

const char *BehaviorTreeWindow::GetDroneFormationLabel(float slotParam)
{
    static thread_local char buf[96];

    const int slot = static_cast<int>(std::lround(slotParam));
    if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
    {
        snprintf(buf, sizeof(buf), "ドローン：データの技（技が選ばれていません）");
        return buf;
    }

    const auto  type = static_cast<DroneFormationType>(slot);
    const char *name = CCL::AI::Formation::FormationRegistry::Instance().DataFormationName(type);

    // 名前が無い＝その枠に陣形が入っていない。指しても静止するので、そう見えるように出す。
    if (!name)
        snprintf(buf, sizeof(buf), "ドローン：枠%d に陣形がありません", slot);
    else
        snprintf(buf, sizeof(buf), "ドローン：%s  (枠%d)", name, slot);
    return buf;
}

