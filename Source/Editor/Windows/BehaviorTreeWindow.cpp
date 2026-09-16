// BehaviorTree エディタの実装
#include "BehaviorTreeWindow.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include <imgui_node_editor.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <functional>
#include <queue>
#include <unordered_map>
#include <unordered_set>
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

namespace
{
    // node-editor が返すID(uintptr_t)を、こちら側のID型(uint32_t)へ戻す。
    //
    // EditorBTNode::id / BTPin::id はどちらも uint32_t で発行しており、それを
    // ライブラリに預けているだけなので、切り詰めても情報は落ちない。
    // 各所で素の static_cast を書くと「なぜ安全か」が散らばるため1箇所に集める。
    inline uint32_t ToEditorId(uintptr_t id) { return static_cast<uint32_t>(id); }

    // コメントグループの配色。
    //   見出し帯・枠線・内側の塗りの3色で1組。役割ごと（索敵／近接／遠距離…）に
    //   塗り分けると、ノードが増えても「どの塊の話か」がズームアウトしたまま分かる。
    //
    // 見出し帯を一番濃くしている理由:
    //   内側の塗りは、下に敷かれるノード背景(StyleColor_NodeBg)と重なるうえ、
    //   面積が広いぶん濃くすると中のノードが読みにくくなる。結果としてどうしても
    //   薄くせざるを得ず、色の違いが分からない。帯は面積が小さく重なりも無いので、
    //   ここを濃くするのが一番効く。
    struct GroupPalette
    {
        const char *name;
        ImU32       header; // 見出し帯（一番目に入る）
        ImU32       border; // 枠線
        ImU32       bg;     // 内側の塗り
    };

    const GroupPalette kGroupPalette[] = {
        {"グレー", IM_COL32(78, 78, 84, 235), IM_COL32(150, 150, 158, 160), IM_COL32(32, 32, 36, 110)},
        {"青", IM_COL32(42, 84, 140, 235), IM_COL32(90, 160, 240, 190), IM_COL32(22, 34, 52, 110)},
        {"緑", IM_COL32(40, 104, 60, 235), IM_COL32(96, 200, 120, 190), IM_COL32(22, 42, 28, 110)},
        {"黄", IM_COL32(126, 104, 36, 235), IM_COL32(230, 196, 90, 190), IM_COL32(46, 40, 20, 110)},
        {"赤", IM_COL32(128, 50, 44, 235), IM_COL32(232, 110, 100, 190), IM_COL32(48, 24, 22, 110)},
        {"紫", IM_COL32(98, 52, 122, 235), IM_COL32(196, 120, 226, 190), IM_COL32(40, 24, 48, 110)},
    };
    constexpr int kGroupPaletteCount = static_cast<int>(sizeof(kGroupPalette) / sizeof(kGroupPalette[0]));

    // 整列の間隔。
    constexpr float kLayoutHGap     = 40.0f;  // 兄弟どうしの間隔
    constexpr float kLayoutVGap     = 90.0f;  // 段と段の間隔
    constexpr float kLayoutTreeGap  = 160.0f; // 根が複数あるときの木と木の間隔
    constexpr float kLayoutDefaultW = 200.0f; // まだ描かれておらずサイズ不明なノードの仮の幅
    constexpr float kLayoutDefaultH = 120.0f; // 同・仮の高さ

    // グループがノードを囲むときの余白と、見出し帯の高さ。
    constexpr float kGroupPad     = 24.0f;
    constexpr float kGroupHeaderH = 30.0f;

    // 「枠(GroupBounds)」と「ノード全体(Bounds)」のズレ。
    //
    // なぜ必要か:
    //   ライブラリはグループを ノード＝パディング＋見出し帯＋枠 という積み方で持つ。
    //   EditorBTNode::width/height は ed::SetGroupSize に渡す値なので「枠」の寸法で、
    //   ed::SetNodePosition に渡す posX/posY は「ノード全体」の左上。単位が違う。
    //   囲みたい矩形から両者を逆算するのに、この差分が要る。
    ImVec2 GroupFrameOffset()
    {
        const ImVec4 pad = ed::GetStyle().NodePadding;
        return ImVec2(pad.x, pad.y + kGroupHeaderH + ImGui::GetStyle().ItemSpacing.y);
    }

    // ノード全体の寸法 − 枠の寸法。
    ImVec2 GroupFrameExtra()
    {
        const ImVec4 pad = ed::GetStyle().NodePadding;
        const ImVec2 off = GroupFrameOffset();
        return ImVec2(off.x + pad.z, off.y + pad.w);
    }
} // namespace

// グラフの構造変化を記録する専用コマンド
class BTGraphChangeCommand : public IEditorCommand
{
    BehaviorTreeWindow *m_window;
    nlohmann::json      m_oldState;
    nlohmann::json      m_newState;
    std::string         m_name;

public:
    BTGraphChangeCommand(BehaviorTreeWindow *window, const std::string &name, const nlohmann::json &oldState, const nlohmann::json &newState)
        : m_window(window), m_name(name), m_oldState(oldState), m_newState(newState)
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
// ActionRegistry ヘルパー（最小構成 BossAction と同期）
// ====================================================================
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

bool BehaviorTreeWindow::DrawActionCombo(const char *label, ActionID &currentId)
{
    bool        changed = false;
    const char *preview = GetActionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview))
    {
        BTCategory lastCategory = BTCategory::None;
        for (int i = 0; i < g_ActionRegistryCount; ++i)
        {
            const ActionEntry &e = g_ActionRegistry[i];
            if (i == 0 || e.category != lastCategory)
            {
                ImGui::SeparatorText(CategoryName(e.category));
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected))
            {
                currentId = e.id;
                changed   = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool BehaviorTreeWindow::DrawConditionCombo(const char *label, ActionID &currentId)
{
    bool        changed = false;
    const char *preview = GetConditionDisplayName(currentId);

    if (ImGui::BeginCombo(label, preview))
    {
        BTCategory lastCategory = BTCategory::None;
        for (int i = 0; i < g_ConditionRegistryCount; ++i)
        {
            const ActionEntry &e = g_ConditionRegistry[i];
            if (i == 0 || e.category != lastCategory)
            {
                ImGui::SeparatorText(CategoryName(e.category));
                lastCategory = e.category;
            }
            const bool selected = (e.id == currentId);
            if (ImGui::Selectable(e.name, selected))
            {
                currentId = e.id;
                changed   = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

BehaviorTreeWindow::BehaviorTreeWindow() : EditorWindow("Behavior Tree Editor")
{

    // =========================================================================
    // ini への保存を無効化する。前回クラッシュ時の不正なノード座標が残ると復帰できないため。
    // =========================================================================
    ed::Config config;
    config.SettingsFile = nullptr; // これにより、常に初期状態でクリーンに起動する
    m_nodeContext       = ed::CreateEditor(&config);

    // 初期状態としてRootノードを1つだけ作成しておく
    CreateNode(EditorBTNodeType::Root, "ROOT", 0.0f, 0.0f);
}

namespace
{
    // ノードをアセットに保存するときのキー文字列。
    //
    // 数値IDではなく名前で保存する。IDは追加のたびに増えるだけの内部都合で、
    // JSONを開いた人間には読めないため。未登録IDは "None" として書き出す
    // （数値IDも併記されるので、情報は失われない）。
    const char *SaveKeyOf(EditorBTNodeType type, ActionID id)
    {
        const ActionEntry *e = (type == EditorBTNodeType::Condition) ? FindConditionEntry(id)
                                                                     : FindActionEntry(id);
        return e ? e->key : "None";
    }

    // 保存キーと param を書き出す。意味を持つノードにだけ書く。
    //
    // Selector/Sequence に "action": "None" が並ぶとJSONが読みづらくなるため。
    void WriteKeyAndParam(nlohmann::json &j, const EditorBTNode &node)
    {
        if (node.type == EditorBTNodeType::Condition)
            j["condition"] = SaveKeyOf(node.type, node.actionOrConditionId);
        else if (node.type == EditorBTNodeType::Action)
            j["action"] = SaveKeyOf(node.type, node.actionOrConditionId);

        // Condition も param を持つ（距離条件の間合いなど）。
        //   以前は行動とデコレータしか引数を持たなかったので条件は書き出していなかった。
        if (node.type == EditorBTNodeType::Action || node.type == EditorBTNodeType::Decorator ||
            node.type == EditorBTNodeType::Condition)
            j["param"] = node.param;
    }

    // 待ち時間がActionIDに埋まっていた頃のグラフを「A_Wait + param」へ読み替える。
    //
    // ランタイム側（BehaviorTreeLoader::MigrateLegacyWait）と同じ変換。
    // エディタで開いて保存し直せば、そのアセットは新形式へ移行する。
    void MigrateLegacyWait(EditorBTNode &node)
    {
        if (node.type != EditorBTNodeType::Action) return;

        static constexpr float kLegacyWaitSeconds[] = {
            0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
        constexpr ActionID kFirst = 100;
        constexpr ActionID kLast  = 111;

        if (node.actionOrConditionId < kFirst || node.actionOrConditionId > kLast) return;

        node.param               = kLegacyWaitSeconds[node.actionOrConditionId - kFirst];
        node.actionOrConditionId = CCL::AI::A_Wait;
    }

    // 間合いがIDに埋まっていた頃の条件を「距離条件 + param」へ読み替える。
    //
    // ランタイム側（BehaviorTreeLoader::MigrateLegacyCondition）と同じ変換で、
    // 対応表は ActionRegistry.h の g_LegacyConditions を共有している。
    // エディタで開いて保存し直せば、そのアセットは新形式へ移行する。
    void MigrateLegacyCondition(EditorBTNode &node, const nlohmann::json &jn)
    {
        if (node.type != EditorBTNodeType::Condition) return;

        const std::string           savedKey = jn.value("condition", std::string());
        const LegacyConditionEntry *legacy =
            FindLegacyCondition(savedKey, node.actionOrConditionId);
        if (!legacy) return;

        node.actionOrConditionId = legacy->newId;
        node.param               = legacy->param;
    }

    // 保存された行動/条件を復元する。キーが正で、数値IDは旧アセット互換。
    void RestoreActionId(EditorBTNode &node, const nlohmann::json &jn)
    {
        node.actionOrConditionId = static_cast<ActionID>(jn.value("actionId", 0));

        const bool  isCondition = (node.type == EditorBTNodeType::Condition);
        const char *keyField    = isCondition ? "condition" : "action";
        if (!jn.contains(keyField) || !jn[keyField].is_string()) return;

        const std::string key = jn[keyField].get<std::string>();
        if (key.empty() || key == "None") return;

        ActionID   resolved = 0;
        const bool found    = isCondition ? TryFindConditionIdByKey(key, resolved)
                                          : TryFindActionIdByKey(key, resolved);
        if (found) node.actionOrConditionId = resolved;
    }
} // namespace

BehaviorTreeWindow::~BehaviorTreeWindow()
{
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}

// ====================================================================
// Undo / Redo 用：現在のグラフ状態をJSONとして抽出する
// ====================================================================
nlohmann::json BehaviorTreeWindow::GetGraphStateAsJson()
{
    nlohmann::json root;
    if (m_nodeContext)
    {
        ed::SetCurrentEditor(m_nodeContext);
    }

    nlohmann::json jNodes = nlohmann::json::array();
    for (auto &node : m_nodes)
    {
        nlohmann::json jn;
        jn["id"]       = node.id;
        jn["type"]     = static_cast<int>(node.type);
        jn["name"]     = node.name;
        jn["actionId"] = static_cast<int>(node.actionOrConditionId);
        WriteKeyAndParam(jn, node);

        // ピンのID
        jn["inputPinId"]  = node.inputPin.id;
        jn["outputPinId"] = node.outputPin.id;

        // 座標情報の取得
        if (node.positionInitialized)
        {
            ImVec2 pos = ed::GetNodePosition(node.id);
            jn["posX"] = pos.x;
            jn["posY"] = pos.y;
        }
        else
        {
            jn["posX"] = node.posX;
            jn["posY"] = node.posY;
        }

        // グループ固有のデータ
        if (node.type == EditorBTNodeType::Group)
        {
            jn["width"]           = node.width;
            jn["height"]          = node.height;
            jn["groupColorIndex"] = node.groupColorIndex;
        }

        // デコレーター固有のデータ
        if (node.type == EditorBTNodeType::Decorator)
        {
            jn["decoratorType"] = node.decoratorType;
        }

        jNodes.push_back(jn);
    }
    root["editor_nodes"] = jNodes;

    nlohmann::json jLinks = nlohmann::json::array();
    for (auto &link : m_links)
    {
        nlohmann::json jl;
        jl["id"]       = link.id;
        jl["startPin"] = link.startPinId;
        jl["endPin"]   = link.endPinId;
        jLinks.push_back(jl);
    }
    root["editor_links"]    = jLinks;
    root["last_id_counter"] = m_idCounter;

    return root;
}

// ====================================================================
// Undo / Redo 用：渡されたJSONからグラフ状態を強制復元する
// ====================================================================
void BehaviorTreeWindow::RestoreGraphState(const nlohmann::json &state)
{
    if (!state.contains("editor_nodes")) return;

    // =========================================================
    // 破棄する前に、現在の選択状態を記憶しておく
    // =========================================================
    std::vector<ed::NodeId> selectedNodes;
    if (m_nodeContext)
    {
        ed::SetCurrentEditor(m_nodeContext);
        selectedNodes.resize(ed::GetSelectedObjectCount());
        int count = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));
        selectedNodes.resize(count);
    }

    // 現在の状態を破棄
    m_nodes.clear();
    m_links.clear();

    if (m_nodeContext)
    {
        ed::SetCurrentEditor(m_nodeContext);
        ed::ClearSelection(); // ゴースト選択状態によるクラッシュを防ぐ
    }

    m_idCounter = state.value("last_id_counter", 1);

    // ノードの復元
    for (auto &jn : state["editor_nodes"])
    {
        EditorBTNode node;
        node.id   = jn["id"];
        node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
        node.name = jn.value("name", "Unknown");
        RestoreActionId(node, jn);
        node.param = jn.value("param", jn.value("decoratorParam", 0.0f));
        MigrateLegacyWait(node);
        MigrateLegacyCondition(node, jn);
        node.posX = jn.value("posX", 0.0f);
        node.posY = jn.value("posY", 0.0f);

        if (node.type == EditorBTNodeType::Group)
        {
            node.width           = jn.value("width", 400.0f);
            node.height          = jn.value("height", 400.0f);
            node.groupColorIndex = jn.value("groupColorIndex", 0);
        }
        if (node.type == EditorBTNodeType::Decorator)
        {
            node.decoratorType = jn.value("decoratorType", 0);
        }

        node.inputPin  = {jn.value("inputPinId", 0u), node.id, true};
        node.outputPin = {jn.value("outputPinId", 0u), node.id, false};

        // 座標をキャンバスに再適用させるため false にしておく
        node.positionInitialized = false;

        m_nodes.push_back(node);
    }

    // リンクの復元
    if (state.contains("editor_links"))
    {
        for (auto &jl : state["editor_links"])
        {
            BTLink link;
            link.id         = jl["id"];
            link.startPinId = jl["startPin"];
            link.endPinId   = jl["endPin"];
            m_links.push_back(link);
        }
    }

    // =========================================================
    //  復元が終わったら、記憶していた選択状態を元に戻す
    // =========================================================
    if (m_nodeContext && !selectedNodes.empty())
    {
        for (auto id : selectedNodes)
        {
            ed::SelectNode(id, true); // true = 複数選択のアペンド
        }
    }

    m_executionOrderDirty = true;
}
void BehaviorTreeWindow::CreateNode(EditorBTNodeType type, const std::string &name, float x, float y)
{
    EditorBTNode node;
    node.id   = GetNextId();
    node.type = type;
    node.name = name;

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

void BehaviorTreeWindow::DrawContents(EditorContext &context)
{
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

/**
 * @brief 毎フレーム、親子関係から実行優先順位を計算する
 * @note DOD（データ指向設計）において、配列の並び順はキャッシュ効率に直結する重要な情報である
 */
void BehaviorTreeWindow::UpdateExecutionOrder()
{
    // 全ての順番を一旦リセット
    for (auto &n : m_nodes)
        n.executionOrder = -1;

    // 1. 各親ノードに対して、接続されている子を取得
    for (auto &parent : m_nodes)
    {
        if (parent.outputPin.id == 0) continue;

        std::vector<EditorBTNode *> children;
        for (const auto &link : m_links)
        {
            if (link.startPinId == parent.outputPin.id)
            {
                for (auto &child : m_nodes)
                {
                    if (child.inputPin.id == link.endPinId)
                    {
                        children.push_back(&child);
                    }
                }
            }
        }

        // 子ノードを X 座標順（左→右）に並べ、その順を実行順とする
        std::sort(children.begin(), children.end(), [](EditorBTNode *a, EditorBTNode *b) {
            return a->posX < b->posX;
        });

        // 3. 順序番号を割り当てる（1から開始）
        for (int i = 0; i < (int)children.size(); ++i)
        {
            children[i]->executionOrder = i + 1;
        }
    }
}

/**
 * @brief ノードエディタ空間（キャンバス）の描画と、リンク操作の監視を行う
 */
void BehaviorTreeWindow::DrawNodeWorkspace(EditorContext &context)
{

    // リンクやノードに変更があった時だけソート処理を走らせる
    if (m_executionOrderDirty)
    {
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

    // 整列はここで実行する。
    //   ed::GetNodeSize が前フレームの実サイズを返すので、種類ごとの高さの違いを
    //   織り込んだ段組みができる。ed::Begin の外だと値が取れない。
    if (m_layoutRequested)
    {
        AutoLayout();
        m_layoutRequested = false;
    }

    // =======================================================
    //  選択中のエンティティから、実行中のAI状態（デバッグ配列）を取得
    // =======================================================
    BehaviorTreeComponent *activeAI = nullptr;
    if (context.selectedEntity != CCL::ECS::InvalidEntityID && context.world)
    {
        activeAI = context.world->GetComponent<BehaviorTreeComponent>(context.selectedEntity);
    }

    uint32_t currentActiveNodeId = 0;
    bool     isActionNodeRunning = false;

    for (auto &node : m_nodes)
    {
        if (!node.positionInitialized)
        {
            ed::SetNodePosition(node.id, ImVec2(node.posX, node.posY));

            // グループの大きさもここで一緒に流し込む。
            //   ed::Group(size) の引数は、一度グループとして登録された後は無視され、
            //   ライブラリが自分の m_GroupBounds を使う。外から変える口はこれだけ。
            //   生成直後・読み込み・Undo・整列のどれもこの分岐を通るので、
            //   ここに置いておけば呼び忘れる経路が無くなる。
            if (node.type == EditorBTNodeType::Group)
                ed::SetGroupSize(node.id, ImVec2(node.width, node.height));

            node.positionInitialized = true;
        }

        // =========================================================
        // グループ（コメント）ノードの専用描画 (安定版ベースのUE5装飾)
        // =========================================================
        if (node.type == EditorBTNodeType::Group)
        {

            // --- A. 選択状態による動的なハイライト（UE5風オレンジ発光） ---
            //   非選択時はグループごとの色を使う。役割で塗り分けると、
            //   ズームアウトしたままでも「どの塊の話か」が読める。
            const int           paletteIndex = (node.groupColorIndex >= 0 && node.groupColorIndex < kGroupPaletteCount)
                                                   ? node.groupColorIndex
                                                   : 0;
            const GroupPalette &palette      = kGroupPalette[paletteIndex];

            bool    isSelected  = ed::IsNodeSelected(node.id);
            ImColor borderColor = isSelected ? ImColor(255, 165, 0, 255) : ImColor(palette.border);
            ImColor bgColor     = ImColor(palette.bg);
            ImColor headerColor = isSelected ? ImColor(150, 95, 20, 245) : ImColor(palette.header);

            // ノード背景は透明にする。
            //   グループもノードなので、まず Bounds 全面に StyleColor_NodeBg
            //   （既定 32,32,32,200）が敷かれる。その上に薄い枠色を重ねても
            //   ほとんど色が出ない。これが「色分けが反映されない」の正体。
            ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(0, 0, 0, 0));
            ed::PushStyleColor(ed::StyleColor_GroupBg, bgColor);
            ed::PushStyleColor(ed::StyleColor_GroupBorder, borderColor);
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 8.0f);
            ed::PushStyleVar(ed::StyleVar_GroupBorderWidth, isSelected ? 3.0f : 2.0f);

            // 色は BeginNode の中で Node へ焼き込まれる（NodeBuilder::Begin）。
            // 実際の描画は ed::End() なので、Push は必ず BeginNode より前に置くこと。
            ed::BeginNode(node.id);
            ImGui::PushID(node.id);

            // --- LOD: ズームに応じてフォントサイズを調整してラベルを描画 ---
            float  currentZoom    = ed::GetCurrentZoom();
            ImVec2 labelScreenPos = ImGui::GetCursorScreenPos();

            // ===============================================================
            // 見出し帯は「高さだけ持つ」Dummy で場所を取る。
            //
            // 掴める範囲がここで決まる:
            //   ライブラリはグループの Header 領域（＝ドラッグと選択ができる帯）を
            //   「ノード上端から枠の上端まで」として計算する。枠の上端は、この
            //   Dummy の下端で決まる。以前は Dummy(1,1) だったため、帯を28px描いて
            //   いても掴めるのは1pxしかなかった。見た目と当たり判定が食い違う原因。
            //
            // 幅を1pxのままにする理由:
            //   幅はそのままノード幅へ反映され、次のフレームの入力になって
            //   毎フレーム膨張する。高さは枠の高さと足し合わされるだけなので安全。
            // ===============================================================
            ImGui::Dummy(ImVec2(1.0f, kGroupHeaderH));

            ImDrawList *groupDrawList = ImGui::GetWindowDrawList();

            // --- B. 見出し帯の描画。上で取った Dummy の範囲にぴったり重ねる ---
            const ImVec4 nodePad    = ed::GetStyle().NodePadding;
            const ImVec2 nodeMin    = ImVec2(labelScreenPos.x - nodePad.x, labelScreenPos.y - nodePad.y);
            const float  bandBottom = labelScreenPos.y + kGroupHeaderH + ImGui::GetStyle().ItemSpacing.y;
            const ImVec2 headerMax  = ImVec2(nodeMin.x + node.width + GroupFrameExtra().x, bandBottom);

            groupDrawList->AddRectFilled(nodeMin, headerMax, headerColor, 8.0f, ImDrawFlags_RoundCornersTop);
            groupDrawList->AddLine(ImVec2(nodeMin.x, headerMax.y), headerMax, ImColor(0, 0, 0, 150), 2.0f);

            if (currentZoom < 0.6f)
            {
                // ズームアウト時はフォントを拡大して視認性を保つ

                // zoom が 0 に近づくと除算結果が発散するため、下限と上限の両方で抑える
                float safeZoom       = (std::max)(0.01f, currentZoom);
                float customFontSize = (std::min)(64.0f, ImGui::GetFontSize() / safeZoom * 0.8f);

                groupDrawList->AddText(
                    ImGui::GetFont(), customFontSize,
                    labelScreenPos, ImColor(235, 235, 235, 255),
                    node.name.c_str());
            }
            else
            {
                // 通常時のテキスト（帯の中に収まる位置へ）
                ImVec2 textPos = ImVec2(nodeMin.x + 10.0f, labelScreenPos.y + 4.0f);
                groupDrawList->AddText(textPos, ImColor(240, 240, 240, 255), node.name.c_str());
            }

            // ed::Group() がリサイズハンドルを自動描画・管理する
            ed::Group(ImVec2(node.width, node.height));

            ImGui::PopID();
            ed::EndNode();

            ed::PopStyleVar(2);
            ed::PopStyleColor(3);

            // EndNode 後に、ユーザーがハンドルで変えたサイズを読み返す。
            //   GetNodeSize が返すのは「ノード全体」。width/height は
            //   ed::SetGroupSize へ渡す「枠」の寸法なので、差分を引いて単位を合わせる。
            //   引かずに入れると、保存と読み込みのたびに枠が余白ぶん太っていく。
            ImVec2       actualSize = ed::GetNodeSize(node.id);
            const ImVec2 extra      = GroupFrameExtra();
            if (actualSize.x > 10.0f && actualSize.y > 10.0f)
            {
                node.width  = (std::max)(100.0f, actualSize.x - extra.x);
                node.height = (std::max)(50.0f, actualSize.y - extra.y);
            }

            // 座標を更新して次のノードへ
            ImVec2 p  = ed::GetNodePosition(node.id);
            node.posX = p.x;
            node.posY = p.y;
            continue;
        }

        // =======================================================
        // 1. スタイリング：ノードの種類に応じた色の決定（UE5準拠）
        // =======================================================
        ImColor headerColor;
        switch (node.type)
        {
        case EditorBTNodeType::Root:
            headerColor = ImColor(40, 40, 40);
            break; // 黒
        case EditorBTNodeType::Selector:
            headerColor = ImColor(120, 30, 30);
            break; // 深い赤
        case EditorBTNodeType::Sequence:
            headerColor = ImColor(30, 80, 120);
            break; // 深い青
        case EditorBTNodeType::Decorator:
            headerColor = ImColor(120, 100, 20);
            break; // 金/黄
        case EditorBTNodeType::Condition:
            headerColor = ImColor(30, 120, 60);
            break; // 緑
        case EditorBTNodeType::Action:
            headerColor = ImColor(40, 100, 150);
            break; // 明るい青
        case EditorBTNodeType::RandomSelector:
            headerColor = ImColor(180, 80, 180); // 紫色にして一目で「運要素」だと分かるようにする
            break;
        default:
            headerColor = ImColor(100, 100, 100);
            break;
        }

        // =======================================================
        //  リアルタイムデバッグ表示（状態による枠線の発光）
        // =======================================================
        ImColor borderColor = ImColor(60, 60, 60, 255); // 通常の枠線色
        float   borderWidth = 1.0f;

        // activeAIが存在し、エディタノードがランタイム配列と紐付いている場合のみ状態を見る
        if (activeAI && node.runtimeFlatIndex >= 0 && node.runtimeFlatIndex < activeAI->debugNodeStates.size())
        {
            BTDebugState state = activeAI->debugNodeStates[node.runtimeFlatIndex];

            if (state == BTDebugState::Running)
            {
                // 実行中（Running）は黄色く脈打つ
                float blink = (sinf(static_cast<float>(ImGui::GetTime()) * 15.0f) * 0.5f) + 0.5f;
                borderColor = ImColor(255, 200, 0, 200 + static_cast<int>(55 * blink));
                borderWidth = 3.0f;

                // 親ノード(Sequence等)もRunningになるが、実際に作業している末端の「Action」ノードを優先してカメラで追う
                if (!isActionNodeRunning || node.type == EditorBTNodeType::Action)
                {
                    currentActiveNodeId = node.id;
                    if (node.type == EditorBTNodeType::Action)
                    {
                        isActionNodeRunning = true;
                    }
                }
            }
            else if (state == BTDebugState::Success)
            {
                // 成功（Success）は緑色
                borderColor = ImColor(50, 255, 50, 200);
                borderWidth = 2.0f;
            }
            else if (state == BTDebugState::Failure)
            {
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
        ImDrawList *drawList   = ImGui::GetWindowDrawList();

        // --- A. 上部入力バー（UE5スタイル） ---
        if (node.inputPin.id != 0)
        {
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

        if (node.executionOrder != -1 && node.type != EditorBTNodeType::Group)
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 0.8f), "Priority: %d", node.executionOrder);
        }

        if (node.type == EditorBTNodeType::Action)
        {
            if (const char *paramLabel = GetActionParamLabel(node.actionOrConditionId))
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "> %s  [%s %.2f]",
                                   GetActionDisplayName(node.actionOrConditionId), paramLabel, node.param);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "> %s",
                                   GetActionDisplayName(node.actionOrConditionId));
            }

            // Waitアクション時の残り時間リアルタイム表示
            if (activeAI && node.runtimeFlatIndex >= 0 && node.runtimeFlatIndex < activeAI->nodeTimers.size())
            {
                float currentTimer = activeAI->nodeTimers[node.runtimeFlatIndex];

                if (currentTimer > 0.0f && IsWaitAction(node.actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Wait: %.1fs", currentTimer);
                }
            }
        }
        else if (node.type == EditorBTNodeType::Condition)
        {
            // 間合いは param に入っているので、ノードの見た目にも出す。
            //   出さないと「距離が N m 未満」が並ぶだけで、どれが何mか分からない。
            if (const char *paramLabel = GetConditionParamLabel(node.actionOrConditionId))
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "? %s  [%s %.1f]",
                                   GetConditionDisplayName(node.actionOrConditionId), paramLabel, node.param);
            }
            else
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "? %s",
                                   GetConditionDisplayName(node.actionOrConditionId));
            }
        }
        // =======================================================
        //  デコレーターの表示とリアルタイム・プログレスバー
        // =======================================================
        else if (node.type == EditorBTNodeType::Decorator)
        {
            const char *decName = "Unknown";
            if (node.decoratorType == 0)
                decName = "Inverter (反転)";
            else if (node.decoratorType == 1)
                decName = "Cooldown (待機)";
            else if (node.decoratorType == 2)
                decName = "Retry (再試行)";

            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "* %s", decName);

            // デバッガー接続時（ゲーム実行中）のみの表示
            if (activeAI && node.runtimeFlatIndex >= 0 && node.runtimeFlatIndex < activeAI->nodeTimers.size())
            {
                if (node.decoratorType == 1)
                { // Cooldownの場合
                    float currentTimer = activeAI->nodeTimers[node.runtimeFlatIndex];
                    if (currentTimer > 0.0f)
                    {
                        // クールダウン中は赤文字で残り秒数を表示
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Cooling: %.1fs", currentTimer);

                        // プログレスバー（ゲージ）の描画
                        float maxTime  = (std::max)(0.001f, node.param);  // ゼロ除算防止
                        float fraction = 1.0f - (currentTimer / maxTime); // 0.0(空) 〜 1.0(満タン)

                        // プログレスバーの色を赤っぽくする
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                        ImGui::ProgressBar(fraction, ImVec2(NODE_WIDTH - 20, 6), ""); // ゲージのみ表示
                        ImGui::PopStyleColor();
                    }
                    else
                    {
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
        if (node.outputPin.id != 0)
        {
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
        ImVec2 p  = ed::GetNodePosition(node.id);
        node.posX = p.x;
        node.posY = p.y;
    }

    // =========================================================
    // 血流の可視化（Execution Flow Debugging）
    // =========================================================
    for (auto &link : m_links)
    {
        // デフォルトの管の状態（非アクティブ）
        ImVec4 linkColor     = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // 白
        float  linkThickness = 2.0f;                           // 細い

        // 1. この管に血液を流し込んでいる「親ノード」を検索する
        auto parentNodeIt = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode &n) {
            return n.outputPin.id == link.startPinId;
        });

        if (parentNodeIt != m_nodes.end())
        {
            // 2. ゲーム実行中(activeAI)かつBake済みのノードなら、ランタイムの配列から状態を取得する
            if (activeAI && parentNodeIt->runtimeFlatIndex >= 0 && parentNodeIt->runtimeFlatIndex < activeAI->debugNodeStates.size())
            {

                // 正しい Enum である BTDebugState を取得
                BTDebugState state = activeAI->debugNodeStates[parentNodeIt->runtimeFlatIndex];

                // 3. 状態に応じた「血流エフェクト」の適用
                if (state == BTDebugState::Running)
                { // 実行中
                    float time  = static_cast<float>(ImGui::GetTime());
                    float pulse = (std::sin(time * 10.0f) + 1.0f) * 0.5f;
                    float alpha = 0.5f + (pulse * 0.5f);

                    linkColor     = ImVec4(1.0f, 0.6f, 0.0f, alpha);
                    linkThickness = 4.0f;
                }
                else if (state == BTDebugState::Success)
                {                                                   // 成功して通過
                    linkColor     = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // 鮮やかな緑
                    linkThickness = 3.0f;
                }
                else if (state == BTDebugState::Failure)
                {                                                   // 失敗して遮断
                    linkColor     = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // 鮮やかな赤
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
    if (m_autoFollow && currentActiveNodeId != 0)
    {
        if (m_lastTrackedNodeId != currentActiveNodeId)
        {

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
            for (auto id : selectedNodes)
            {
                ed::SelectNode(id, true); // true = 追加選択
            }

            m_lastTrackedNodeId = currentActiveNodeId;
        }
    }
    else if (currentActiveNodeId == 0)
    {
        m_lastTrackedNodeId = 0; // 実行が止まったらリセット
    }

    if (ed::BeginCreate())
    {
        ed::PinId startPinId = 0, endPinId = 0;
        if (ed::QueryNewLink(&startPinId, &endPinId))
        {
            if (startPinId && endPinId)
            {

                // 1. ピンの属性（InputかOutputか）を特定する
                bool startIsInput = false;
                bool endIsInput   = false;
                for (const auto &n : m_nodes)
                {
                    if (n.inputPin.id == startPinId.Get()) startIsInput = true;
                    if (n.inputPin.id == endPinId.Get()) endIsInput = true;
                }

                // 2. Input同士、Output同士の接続は赤線にして拒否する
                if (startIsInput == endIsInput)
                {
                    ed::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
                else
                {
                    if (ed::AcceptNewItem())
                    {
                        BTLink newLink;
                        newLink.id = GetNextId();

                        // 3. ユーザーのドラッグ方向に依存せず、必ず startPinId に「出力ピン(親)」、endPinId に「入力ピン(子)」を入れる
                        if (startIsInput)
                        {
                            newLink.startPinId = ToEditorId(endPinId.Get());
                            newLink.endPinId   = ToEditorId(startPinId.Get());
                        }
                        else
                        {
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
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
    {
        // 2. Ctrl + D キーで選択ノードを瞬時に複製 (Duplicate) のみを残す
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            DuplicateSelectedNodes();
        }

        // Ctrl + G で選択ノードをコメント枠で囲む
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G))
        {
            GroupSelectedNodes();
        }
    }

    bool itemsDeleted = false;
    if (ed::BeginDelete())
    {
        ed::NodeId deletedNodeId = 0;
        while (ed::QueryDeletedNode(&deletedNodeId))
        {
            if (ed::AcceptDeletedItem())
            {
                DeleteNodeAndLinks(ToEditorId(deletedNodeId.Get()));
                itemsDeleted = true;
            }
        }
        ed::LinkId deletedLinkId = 0;
        while (ed::QueryDeletedLink(&deletedLinkId))
        {
            if (ed::AcceptDeletedItem())
            {
                m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                             [&](const BTLink &l) { return l.id == deletedLinkId.Get(); }),
                              m_links.end());
                m_executionOrderDirty = true;
                itemsDeleted          = true;
            }
        }
    }
    ed::EndDelete();

    // 削除が完了したらUndoスタックに記録する
    if (itemsDeleted)
    {
        RecordStateForUndo("Delete Node/Link");
    }

    HandleContextMenu();

    // 初回のズーム合わせは ed::End() より前で行う。ノードサイズが確定するまで数フレーム待つ。
    if (m_needInitialZoom)
    {
        m_framesSinceOpen++;

        // ImGuiがノードの幅と高さを完全に計算し終わるまで待つ（3フレーム目以降）
        if (m_framesSinceOpen > 2)
        {

            // NavigateToContent 呼び出し時のフリーズ・NaN 対策
            // 1. ノードが無い状態で NavigateToContent を呼ぶとゼロ除算で死ぬため回避
            // 2. 引数に 0.0f を渡して「アニメーション無し(即時反映)」にし、カメラ補間計算中の NaN 増殖を封じる
            if (!m_nodes.empty())
            {
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
void BehaviorTreeWindow::DrawInspector()
{
    ImGui::Text("インスペクタ (Inspector)");
    ImGui::Separator();

    std::vector<ed::NodeId> selectedNodes;
    selectedNodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size()));

    if (nodeCount > 0)
    {
        uint32_t selectedId = ToEditorId(selectedNodes[0].Get());
        auto     it         = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const EditorBTNode &n) { return n.id == selectedId; });

        if (it != m_nodes.end())
        {
            // =========================================================
            // Undoを安全に記録するための遅延フラグ
            // =========================================================
            bool        needsUndo   = false;
            std::string undoMessage = "Inspector Changed";

            // 機能追加: ノード名の変更（コメントや整理に必須）
            char nameBuf[256];
            strcpy_s(nameBuf, it->name.c_str());
            if (ImGui::InputText("名前 (Name)", nameBuf, sizeof(nameBuf)))
            {
                it->name = nameBuf;
            }

            // 入力が完了（エンターかフォーカス外れ）した時だけUndo記録
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                needsUndo   = true;
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

            switch (it->type)
            {
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
            if (it->type == EditorBTNodeType::Group)
            {
                ImGui::Text("枠の色:");
                // 色見本を横に並べる。名前のコンボより、押した結果が想像しやすい。
                for (int i = 0; i < kGroupPaletteCount; ++i)
                {
                    if (i > 0) ImGui::SameLine();
                    ImGui::PushID(i);
                    const bool active = (it->groupColorIndex == i);
                    if (ImGui::ColorButton(kGroupPalette[i].name,
                                           ImGui::ColorConvertU32ToFloat4(kGroupPalette[i].header),
                                           ImGuiColorEditFlags_NoAlpha,
                                           ImVec2(active ? 28.0f : 22.0f, 22.0f)))
                    {
                        it->groupColorIndex = i;
                        needsUndo           = true;
                        undoMessage         = "Change Group Color";
                    }
                    ImGui::PopID();
                }
                ImGui::TextDisabled("上の「名前 (Name)」がそのまま枠の見出しになります");
                ImGui::Separator();
            }

            if (it->type == EditorBTNodeType::Condition)
            {
                ImGui::Separator();
                ImGui::Text("条件設定 (Condition):");
                if (DrawConditionCombo("条件を選ぶ", it->actionOrConditionId))
                {
                    m_executionOrderDirty = true;
                    needsUndo             = true;
                    undoMessage           = "Change Condition";

                    // 引数を使う条件へ切り替えたのに 0 のままだと「距離0m未満」に
                    // なって永久に成立しない。最初の1回だけ既定値を入れておく。
                    if (GetConditionParamLabel(it->actionOrConditionId) && it->param <= 0.0f)
                        it->param = 10.0f;
                }

                // 引数を持つ条件（間合いなど）だけ入力欄を出す。行動側と同じ仕組み。
                if (const char *paramLabel = GetConditionParamLabel(it->actionOrConditionId))
                {
                    if (ImGui::DragFloat(paramLabel, &it->param, 0.5f, 0.0f, 500.0f, "%.1f"))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Condition Param";
                    }
                }
            }
            else if (it->type == EditorBTNodeType::Action)
            {
                ImGui::Separator();
                ImGui::Text("行動設定 (Action):");
                if (DrawActionCombo("行動を選ぶ", it->actionOrConditionId))
                {
                    m_executionOrderDirty = true;
                    needsUndo             = true;
                    undoMessage           = "Change Action";

                    // 引数を使う行動へ切り替えたのに 0 のままだと「置いたのに何も起きない」
                    // ように見えるので、最初の1回だけ既定値を入れておく。
                    if (GetActionParamLabel(it->actionOrConditionId) && it->param <= 0.0f)
                        it->param = 1.0f;
                }

                // 引数を持つ行動（待機の秒数、近接の硬直など）だけ入力欄を出す
                if (const char *paramLabel = GetActionParamLabel(it->actionOrConditionId))
                {
                    if (ImGui::DragFloat(paramLabel, &it->param, 0.1f, 0.0f, 60.0f, "%.2f"))
                    {
                        needsUndo   = true;
                        undoMessage = "Change Action Param";
                    }
                }

                // 選択中アクションの補足（最小構成 BossAction との対応）
                if (IsBossPhysicalAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f),
                                       "→ BossActionSystem が物理移動を実行");
                }
                else if (IsDroneAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f),
                                       "→ BossCommand.requestFormation に陣形を指示");
                }
                else if (IsWaitAction(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                                       "→ nodeTimers で待機（BossAction 非依存）");
                }
                else if (it->actionOrConditionId == CCL::AI::A_ResetDamage)
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                       "→ ブラックボードの蓄積ダメージをリセット");
                }
                else if (!FindActionEntry(it->actionOrConditionId))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                                       "⚠ 未登録ID（旧BTからの互換データの可能性）");
                }
            }

            if (it->type == EditorBTNodeType::Decorator)
            {
                ImGui::Separator();
                ImGui::Text("デコレーター設定 (Decorator):");

                const char *decNames[] = {"0: 結果反転 (Inverter)", "1: クールダウン (Cooldown)", "2: リトライ (Retry)", "3: 非同期 (Async)"};
                if (ImGui::Combo("タイプ", &it->decoratorType, decNames, IM_ARRAYSIZE(decNames)))
                {
                    // 既定値を入れておく。0 のままだと Cooldown は毎フレーム素通り、
                    // Retry は子を一度も評価しない＝どちらも「置いたのに効かない」になる。
                    // 行動・条件の param が同じ理由で自動補完されるのに揃える。
                    if (it->param <= 0.0f)
                    {
                        if (it->decoratorType == 1)
                            it->param = 5.0f; // Cooldown [秒]
                        else if (it->decoratorType == 2)
                            it->param = 3.0f; // Retry [回]
                    }
                    m_executionOrderDirty = true;
                    needsUndo             = true;
                    undoMessage           = "Change Decorator Type";
                }

                if (it->decoratorType == 1)
                { // Cooldown
                    // ドラッグ中は数値を変えるだけ
                    ImGui::DragFloat("待機時間 (秒)", &it->param, 0.1f, 0.0f, 60.0f);

                    // マウスを離した時だけUndo記録
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        needsUndo   = true;
                        undoMessage = "Change Cooldown Time";
                    }
                }
                else if (it->decoratorType == 2)
                { // Retry
                    int retryCount = static_cast<int>(it->param);
                    if (ImGui::DragInt("リトライ回数", &retryCount, 1, 1, 10))
                    {
                        it->param = static_cast<float>(retryCount);
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        needsUndo   = true;
                        undoMessage = "Change Retry Count";
                    }
                }
            }

            // =========================================================
            // すべての `it` の使用が終わった「一番最後」にUndoを記録する！
            // これによりイテレータ無効化によるクラッシュを完全に防ぐ
            // =========================================================
            if (needsUndo)
            {
                RecordStateForUndo(undoMessage);
            }
        }
    }
    else
    {
        ImGui::TextDisabled("編集するノードを選択してください。");
    }
}

// ====================================================================
// ノードと関連リンクの安全な連鎖削除
// ====================================================================
void BehaviorTreeWindow::DeleteNodeAndLinks(uint32_t nodeId)
{
    // 1. 削除対象のノードを探す
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode &n) { return n.id == nodeId; });
    if (it == m_nodes.end()) return;

    // 2. そのノードが持っているピンのIDを取得
    uint32_t inPinId  = it->inputPin.id;
    uint32_t outPinId = it->outputPin.id;

    // 3. このピンに繋がっているリンクを全て配列から消し去る
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [inPinId, outPinId](const BTLink &link) {
                                     return link.startPinId == outPinId || link.endPinId == inPinId;
                                 }),
                  m_links.end());

    // 4. 最後にノード本体を消す
    m_nodes.erase(it);

    m_executionOrderDirty = true;

    CCL_LOG_INFO(LogCategory::Editor, "Node(ID:%u) and its connected links were deleted.", nodeId);
}

// ====================================================================
// 自動整列
// ====================================================================
std::vector<uint32_t> BehaviorTreeWindow::CollectNodesInGroup(uint32_t groupId) const
{
    std::vector<uint32_t> result;

    const ImVec2 gPos  = ed::GetNodePosition(groupId);
    const ImVec2 gSize = ed::GetNodeSize(groupId);
    if (gSize.x <= 0.0f || gSize.y <= 0.0f) return result; // まだ一度も描かれていない

    for (const auto &n : m_nodes)
    {
        if (n.id == groupId) continue;
        if (n.type == EditorBTNodeType::Group) continue; // 入れ子は今は扱わない

        const ImVec2 p  = ed::GetNodePosition(n.id);
        const ImVec2 s  = ed::GetNodeSize(n.id);
        const float  cx = p.x + s.x * 0.5f;
        const float  cy = p.y + s.y * 0.5f;

        if (cx >= gPos.x && cx <= gPos.x + gSize.x &&
            cy >= gPos.y && cy <= gPos.y + gSize.y)
            result.push_back(n.id);
    }
    return result;
}

void BehaviorTreeWindow::AutoLayout()
{
    if (m_nodes.empty()) return;

    RecordStateForUndo("Auto Layout");

    // IDからノードを引く表。毎回 find_if すると O(N^2) になる。
    std::unordered_map<uint32_t, EditorBTNode *> byId;
    for (auto &n : m_nodes)
        byId[n.id] = &n;

    auto find = [&](uint32_t id) -> EditorBTNode * {
        auto it = byId.find(id);
        return (it == byId.end()) ? nullptr : it->second;
    };

    // 実サイズ。まだ描かれていないノードは仮値で置く。
    auto sizeOf = [&](uint32_t id) {
        const ImVec2 s = ed::GetNodeSize(id);
        return ImVec2((s.x > 1.0f) ? s.x : kLayoutDefaultW,
                      (s.y > 1.0f) ? s.y : kLayoutDefaultH);
    };

    // ---------------------------------------------------------------
    // 1. グループが今どのノードを囲んでいるかを控える
    //    ノードを動かした後では判定できないので、必ず動かす前に取る。
    // ---------------------------------------------------------------
    struct GroupSnapshot
    {
        uint32_t              id;
        std::vector<uint32_t> members;
    };
    std::vector<GroupSnapshot> groups;
    for (const auto &n : m_nodes)
        if (n.type == EditorBTNodeType::Group)
            groups.push_back({n.id, CollectNodesInGroup(n.id)});

    // ---------------------------------------------------------------
    // 2. 親→子のリストを作る
    // ---------------------------------------------------------------
    std::unordered_map<uint32_t, std::vector<uint32_t>> childrenOf;
    std::unordered_set<uint32_t>                        hasParent;

    for (const auto &link : m_links)
    {
        const EditorBTNode *parent = nullptr;
        const EditorBTNode *child  = nullptr;
        for (const auto &n : m_nodes)
        {
            if (n.outputPin.id != 0 && n.outputPin.id == link.startPinId) parent = &n;
            if (n.inputPin.id != 0 && n.inputPin.id == link.endPinId) child = &n;
        }
        if (!parent || !child) continue;

        childrenOf[parent->id].push_back(child->id);
        hasParent.insert(child->id);
    }

    // ここが整列の安全性の要。子を「今のX順」に並べてから配置するので、
    // 相対順序が保たれ、実行順は整列の前後で変わらない。
    for (auto &kv : childrenOf)
    {
        std::sort(kv.second.begin(), kv.second.end(), [&](uint32_t a, uint32_t b) {
            const EditorBTNode *na = find(a);
            const EditorBTNode *nb = find(b);
            if (!na || !nb) return false;
            return na->posX < nb->posX;
        });
    }

    // ---------------------------------------------------------------
    // 3. 部分木の幅と深さを求める（葉から根へ）
    // ---------------------------------------------------------------
    std::unordered_map<uint32_t, float> widthOf;
    std::unordered_map<uint32_t, int>   depthOf;
    std::unordered_set<uint32_t>        visited;

    std::function<float(uint32_t, int)> measure = [&](uint32_t id, int depth) -> float {
        // 循環リンクや、1つの子に複数の親が繋がった形でも止まるようにする。
        if (!visited.insert(id).second) return 0.0f;

        depthOf[id]     = depth;
        const float own = sizeOf(id).x;

        auto it = childrenOf.find(id);
        if (it == childrenOf.end() || it->second.empty())
        {
            widthOf[id] = own;
            return own;
        }

        float sum = 0.0f;
        for (uint32_t c : it->second)
        {
            const float w = measure(c, depth + 1);
            if (w <= 0.0f) continue; // 既に別経路で置かれた子は幅を数えない
            if (sum > 0.0f) sum += kLayoutHGap;
            sum += w;
        }

        widthOf[id] = (std::max)(own, sum);
        return widthOf[id];
    };

    // 根＝親を持たないノード。Root 型を必ず先頭に置く。
    std::vector<uint32_t> roots;
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group) continue;
        if (hasParent.count(n.id) == 0) roots.push_back(n.id);
    }
    std::stable_sort(roots.begin(), roots.end(), [&](uint32_t a, uint32_t b) {
        const EditorBTNode *na = find(a);
        const EditorBTNode *nb = find(b);
        const bool          ra = (na && na->type == EditorBTNodeType::Root);
        const bool          rb = (nb && nb->type == EditorBTNodeType::Root);
        return ra && !rb;
    });

    for (uint32_t r : roots)
        measure(r, 0);

    // どの根からも辿れなかったノード（リンクが循環している塊など）も置き去りにしない。
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group) continue;
        if (visited.count(n.id)) continue;
        roots.push_back(n.id);
        measure(n.id, 0);
    }

    if (roots.empty()) return;

    // ---------------------------------------------------------------
    // 4. 段ごとの高さを揃える
    //    ノードは種類で高さが違う（Actionのゲージ付きが一番高い）。
    //    段の中の最大値で行の高さを決めると、線が斜めに交差しなくなる。
    // ---------------------------------------------------------------
    int maxDepth = 0;
    for (const auto &kv : depthOf)
        maxDepth = (std::max)(maxDepth, kv.second);

    std::vector<float> rowHeight(static_cast<size_t>(maxDepth) + 1, kLayoutDefaultH);
    for (const auto &kv : depthOf)
    {
        const float h = sizeOf(kv.first).y;
        float      &r = rowHeight[static_cast<size_t>(kv.second)];
        if (h > r) r = h;
    }

    std::vector<float> rowTop(rowHeight.size(), 0.0f);
    for (size_t d = 1; d < rowTop.size(); ++d)
        rowTop[d] = rowTop[d - 1] + rowHeight[d - 1] + kLayoutVGap;

    // ---------------------------------------------------------------
    // 5. 根から葉へ配置する
    // ---------------------------------------------------------------
    std::unordered_set<uint32_t> placed;

    std::function<void(uint32_t, float)> place = [&](uint32_t id, float left) {
        if (!placed.insert(id).second) return;

        EditorBTNode *n = find(id);
        if (!n) return;

        const ImVec2 sz = sizeOf(id);
        const float  w  = widthOf.count(id) ? widthOf[id] : sz.x;

        // 自分は割り当てられた領域の中央に置く。子の重心の上に来る。
        n->posX                = left + (w - sz.x) * 0.5f;
        n->posY                = rowTop[static_cast<size_t>(depthOf[id])];
        n->positionInitialized = false; // 描画ループが ed::SetNodePosition を呼ぶ

        auto it = childrenOf.find(id);
        if (it == childrenOf.end()) return;

        float cursor = left;
        for (uint32_t c : it->second)
        {
            if (placed.count(c)) continue;
            place(c, cursor);
            cursor += (widthOf.count(c) ? widthOf[c] : kLayoutDefaultW) + kLayoutHGap;
        }
    };

    float treeLeft = 0.0f;
    for (uint32_t r : roots)
    {
        if (placed.count(r)) continue;
        place(r, treeLeft);
        treeLeft += (widthOf.count(r) ? widthOf[r] : kLayoutDefaultW) + kLayoutTreeGap;
    }

    // ---------------------------------------------------------------
    // 6. グループ枠を、囲んでいたノードに合わせ直す
    //    これをやらないと、整列でノードだけが動いて枠が空のまま取り残される。
    // ---------------------------------------------------------------
    for (const auto &g : groups)
    {
        if (g.members.empty()) continue; // 空の枠は動かしようがないので触らない

        float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
        int   used = 0;
        for (uint32_t m : g.members)
        {
            const EditorBTNode *n = find(m);
            if (!n || !placed.count(m)) continue;
            const ImVec2 s = sizeOf(m);
            minX           = (std::min)(minX, n->posX);
            minY           = (std::min)(minY, n->posY);
            maxX           = (std::max)(maxX, n->posX + s.x);
            maxY           = (std::max)(maxY, n->posY + s.y);
            ++used;
        }
        if (used == 0) continue;

        EditorBTNode *gn = find(g.id);
        if (!gn) continue;

        // width/height は「枠」、posX/posY は「ノード全体の左上」。
        // 単位が違うので GroupFrameOffset のぶんだけ左上へずらす。
        const ImVec2 off = GroupFrameOffset();
        gn->posX         = minX - kGroupPad - off.x;
        gn->posY         = minY - kGroupPad - off.y;
        gn->width        = (maxX - minX) + kGroupPad * 2.0f;
        gn->height       = (maxY - minY) + kGroupPad * 2.0f;
        // サイズの反映は positionInitialized=false に任せる（描画ループ側に集約済み）。
        gn->positionInitialized = false;
    }

    m_executionOrderDirty = true;
    CCL_LOG_INFO(LogCategory::Editor, "[BT] 整列しました（%zu ノード / %zu グループ）",
                 placed.size(), groups.size());
}

// ====================================================================
// 選択ノードをコメントで囲む
// ====================================================================
void BehaviorTreeWindow::GroupSelectedNodes()
{
    std::vector<ed::NodeId> selected;
    selected.resize(ed::GetSelectedObjectCount());
    const int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
    if (count <= 0) return;

    float minX = FLT_MAX, minY = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX;
    int   used = 0;

    for (int i = 0; i < count; ++i)
    {
        const uint32_t id = ToEditorId(selected[i].Get());

        auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
                               [&](const EditorBTNode &n) { return n.id == id; });
        if (it == m_nodes.end()) continue;
        if (it->type == EditorBTNodeType::Group) continue; // 枠で枠を囲まない

        const ImVec2 p = ed::GetNodePosition(id);
        const ImVec2 s = ed::GetNodeSize(id);
        if (s.x <= 1.0f || s.y <= 1.0f) continue;

        minX = (std::min)(minX, p.x);
        minY = (std::min)(minY, p.y);
        maxX = (std::max)(maxX, p.x + s.x);
        maxY = (std::max)(maxY, p.y + s.y);
        ++used;
    }

    if (used == 0)
    {
        CCL_LOG_WARN(LogCategory::Editor, "[BT] 囲めるノードが選択されていません");
        return;
    }

    RecordStateForUndo("Group Selected Nodes");

    // width/height は「枠」、座標は「ノード全体の左上」。単位を合わせる。
    const ImVec2 off = GroupFrameOffset();
    CreateNode(EditorBTNodeType::Group, "コメント",
               minX - kGroupPad - off.x, minY - kGroupPad - off.y);

    // サイズは描画ループが ed::SetGroupSize で反映する（CreateNode が
    // positionInitialized=false で作るため、必ずその分岐を通る）。
    EditorBTNode &g = m_nodes.back();
    g.width         = (maxX - minX) + kGroupPad * 2.0f;
    g.height        = (maxY - minY) + kGroupPad * 2.0f;
}

// ====================================================================
// 右クリックコンテキストメニュー処理
// ====================================================================
void BehaviorTreeWindow::HandleContextMenu()
{
    ed::Suspend();

    // =======================================================
    //  Spaceキーでマウスカーソルの位置にメニューを出す
    // =======================================================
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space))
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePos(); // Spaceを押した瞬間のマウス座標を記憶
    }

    if (ed::ShowBackgroundContextMenu())
    {
        ImGui::OpenPopup("Create New Node");
        m_newNodeSpawnPos = ImGui::GetMousePosOnOpeningCurrentPopup();
    }

    if (ImGui::BeginPopup("Create New Node"))
    {
        ImVec2 canvasPos = ed::ScreenToCanvas(m_newNodeSpawnPos);

        if (ImGui::MenuItem("セレクター (Selector)")) CreateNode(EditorBTNodeType::Selector, "セレクター (Selector)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("シーケンス (Sequence)")) CreateNode(EditorBTNodeType::Sequence, "シーケンス (Sequence)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("ランダムセレクター（Random Selector）"))
        {
            CreateNode(EditorBTNodeType::RandomSelector, "ランダムセレクター（Random Selector）", canvasPos.x, canvasPos.y);
        }
        ImGui::Separator();

        if (ImGui::MenuItem("デコレーター (Decorator)")) CreateNode(EditorBTNodeType::Decorator, "デコレーター (Decorator)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("コンディション (Condition)")) CreateNode(EditorBTNodeType::Condition, "コンディション (Condition)", canvasPos.x, canvasPos.y);
        if (ImGui::MenuItem("アクション (Action)")) CreateNode(EditorBTNodeType::Action, "アクション (Action)", canvasPos.x, canvasPos.y);
        ImGui::Separator();
        // 選択ノードを囲むのが本命。空の枠を置いてから手で広げるのは最後の手段。
        if (ImGui::MenuItem("選択ノードをコメントで囲む", "Ctrl+G")) GroupSelectedNodes();
        if (ImGui::MenuItem("空のコメント枠を置く")) CreateNode(EditorBTNodeType::Group, "コメント", canvasPos.x, canvasPos.y);

        ImGui::Separator();
        // 複数ノードの複製
        if (ImGui::MenuItem("選択ノードを複製 (Duplicate)"))
        {
            DuplicateSelectedNodes();
        }

        ImGui::EndPopup();
    }

    ed::Resume();
}

// ====================================================================
// セーブ処理：ピンIDを明示的に保存するように拡張
// ====================================================================
void BehaviorTreeWindow::SaveGraph(const std::string &path)
{
    json root;
    ed::SetCurrentEditor(m_nodeContext);

    // ノード情報の保存
    json jNodes = json::array();
    for (auto &node : m_nodes)
    {
        json jn;
        jn["id"]       = node.id;
        jn["type"]     = static_cast<int>(node.type);
        jn["name"]     = node.name;
        jn["actionId"] = static_cast<int>(node.actionOrConditionId);
        WriteKeyAndParam(jn, node);

        // ピンのIDを保存。これがリンクの復元に不可欠。
        jn["inputPinId"]  = node.inputPin.id;
        jn["outputPinId"] = node.outputPin.id;

        ImVec2 pos = ed::GetNodePosition(node.id);
        jn["posX"] = pos.x;
        jn["posY"] = pos.y;
        // グループサイズを保存
        if (node.type == EditorBTNodeType::Group)
        {
            jn["width"]           = node.width;
            jn["height"]          = node.height;
            jn["groupColorIndex"] = node.groupColorIndex;
        }
        // デコレーターのパラメータを保存する
        if (node.type == EditorBTNodeType::Decorator)
        {
            jn["decoratorType"] = node.decoratorType;
        }

        jNodes.push_back(jn);
    }
    root["editor_nodes"] = jNodes;

    // リンク情報の保存
    json jLinks = json::array();
    for (auto &link : m_links)
    {
        json jl;
        jl["id"]       = link.id;
        jl["startPin"] = link.startPinId;
        jl["endPin"]   = link.endPinId;
        jLinks.push_back(jl);
    }
    root["editor_links"]    = jLinks;
    root["last_id_counter"] = m_idCounter;

    // =======================================================
    //  ここで BuildBakedNodes() を呼び出して統合する
    // =======================================================
    root["nodes"]   = BuildBakedNodes();
    root["assetId"] = 1;

    // 書き出し（1回だけファイルを開く）
    // ファイル書き込みは FileSystem 経由で行う
    std::string jsonStr = root.dump(4);
    if (FileSystem::WriteAllText(path, jsonStr))
    {
        m_dirty = false;
        CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフを保存＆Bakeしました: %s", path.c_str());
    }
    else
    {
        // 失敗時に何も出ていなかった。保存できたつもりで編集を続けるのが一番まずい。
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフの保存に失敗しました: %s", path.c_str());
    }
}

// =========================================================
// セーブ処理の統合 (上書きバグ解消 / 新children配列対応版)
// =========================================================
nlohmann::json BehaviorTreeWindow::BuildBakedNodes()
{
    nlohmann::json jsonNodes = nlohmann::json::array();
    if (m_nodes.empty()) return jsonNodes;

    // 1. Rootノードを探す
    EditorBTNode *rootNode = nullptr;
    for (auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Root)
        {
            rootNode = &n;
            break;
        }
    }
    if (!rootNode) return jsonNodes;

    // ヘルパー：特定の出力ピンから繋がっている子ノードを「X座標順」に取得する
    auto getSortedChildren = [&](uint32_t outputPinId) {
        std::vector<EditorBTNode *> children;
        for (const auto &link : m_links)
        {
            if (link.startPinId == outputPinId)
            {
                for (auto &n : m_nodes)
                {
                    if (n.inputPin.id == link.endPinId)
                    {
                        children.push_back(&n);
                        break;
                    }
                }
            }
        }
        std::sort(children.begin(), children.end(), [](EditorBTNode *a, EditorBTNode *b) {
            return a->posX < b->posX;
        });
        return children;
    };

    auto rootChildren = getSortedChildren(rootNode->outputPin.id);
    if (rootChildren.empty()) return jsonNodes;

    // 2. BFS（幅優先探索）によるDODフラット配列の構築
    struct BakeTask
    {
        EditorBTNode *editorNode;
        int           flatIndex;
    };
    std::queue<BakeTask> tasks;

    auto getRuntimeTypeString = [](EditorBTNodeType type) {
        switch (type)
        {
        case EditorBTNodeType::Selector:
            return "Selector";
        case EditorBTNodeType::Sequence:
            return "Sequence";
        case EditorBTNodeType::Condition:
            return "Condition";
        case EditorBTNodeType::Action:
            return "Action";
        case EditorBTNodeType::Decorator:
            return "Decorator";
        case EditorBTNodeType::RandomSelector:
            return "RandomSelector";

        default:
            return "Unknown";
        }
    };

    // =======================================================
    // ユーザーがROOTに複数繋いでしまった場合の警告
    // =======================================================
    if (rootChildren.size() > 1)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "【BTエラー】ROOTノードには複数の線を繋げません！必ず1つの「Selector」等に繋いでから分岐させてください。");
    }

    EditorBTNode *actualRoot = rootChildren[0];

    nlohmann::json rootJson;
    rootJson["type"]                = getRuntimeTypeString(actualRoot->type);
    rootJson["actionOrConditionId"] = static_cast<int>(actualRoot->actionOrConditionId);
    WriteKeyAndParam(rootJson, *actualRoot);

    // 子ノードと同じく decoratorType を書く。
    //   ここが抜けていたため、ROOT 直下にデコレーターを置くと nodes[] に種類が残らず、
    //   読み込み側の既定値 0（Inverter）として解釈されていた。
    //   ＝「Cooldown を置いたのに反転として動く」＝クールダウンが効かない。
    if (actualRoot->type == EditorBTNodeType::Decorator)
    {
        rootJson["decoratorType"] = actualRoot->decoratorType;
    }

    rootJson["children"] = nlohmann::json::array(); // 配列として初期化
    jsonNodes.push_back(rootJson);

    tasks.push({actualRoot, 0});

    while (!tasks.empty())
    {
        BakeTask task = tasks.front();
        tasks.pop();

        auto children = getSortedChildren(task.editorNode->outputPin.id);
        if (children.empty()) continue;

        // 子供たちを配列に追加し、次のキューへ
        for (EditorBTNode *child : children)
        {
            nlohmann::json childJson;
            childJson["type"]                = getRuntimeTypeString(child->type);
            childJson["actionOrConditionId"] = static_cast<int>(child->actionOrConditionId);
            WriteKeyAndParam(childJson, *child);
            childJson["children"] = nlohmann::json::array(); // 配列として初期化

            // デコレーター情報のBake
            if (child->type == EditorBTNodeType::Decorator)
            {
                childJson["decoratorType"] = child->decoratorType;
            }

            child->runtimeFlatIndex = static_cast<int>(jsonNodes.size());

            // 親ノードの "children" 配列に、この子のインデックスを追加する
            jsonNodes[task.flatIndex]["children"].push_back(jsonNodes.size());

            jsonNodes.push_back(childJson);
            tasks.push({child, static_cast<int>(jsonNodes.size() - 1)});
        }
    }

    return jsonNodes;
}

// ====================================================================
// ロード処理：ピンIDを生成せず、保存された値で復元する
// ====================================================================
void BehaviorTreeWindow::LoadGraph(const std::string &path)
{
    // ファイル読み込みは FileSystem 経由で行う
    std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty()) return;

    json j;
    try
    {
        j = json::parse(jsonStr);
    }
    catch (...)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフのパースに失敗しました: %s", path.c_str());
        return;
    }

    m_nodes.clear();
    m_links.clear();
    m_idCounter = j.value("last_id_counter", 1);

    // ノードの復元
    for (auto &jn : j["editor_nodes"])
    {
        EditorBTNode node;
        node.id   = jn["id"];
        node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
        node.name = jn["name"];
        RestoreActionId(node, jn);
        node.param = jn.value("param", jn.value("decoratorParam", 0.0f));
        MigrateLegacyWait(node);
        MigrateLegacyCondition(node, jn);
        node.posX = jn["posX"];
        node.posY = jn["posY"];
        // グループサイズを復元
        if (node.type == EditorBTNodeType::Group)
        {
            node.width           = jn.value("width", 300.0f);
            node.height          = jn.value("height", 200.0f);
            node.groupColorIndex = jn.value("groupColorIndex", 0);
        }

        // =======================================================
        // デコレーターのパラメータを復元する
        // =======================================================
        if (node.type == EditorBTNodeType::Decorator)
        {
            node.decoratorType = jn.value("decoratorType", 0);
        }

        // GetNextId() を使わず、保存されたIDをそのままセットする
        node.inputPin  = {jn.value("inputPinId", 0u), node.id, true};
        node.outputPin = {jn.value("outputPinId", 0u), node.id, false};

        m_nodes.push_back(node);
    }

    // リンクの復元
    for (auto &jl : j["editor_links"])
    {
        BTLink link;
        link.id         = jl["id"];
        link.startPinId = jl["startPin"];
        link.endPinId   = jl["endPin"];
        m_links.push_back(link);
    }

    // =======================================================
    // ロード完了直後に「空回し」のBakeを実行する！
    // これにより、わざわざ保存しなくても `runtimeFlatIndex` が計算され、
    // ロードした瞬間からリアルタイムデバッグが可能になる。
    // =======================================================
    BuildBakedNodes();

    m_needInitialZoom = true;

    // Undo の基準をこのグラフに引き直す。
    //   これを忘れると m_lastSavedState は「前に開いていたBT」のままなので、
    //   ロード後の最初の編集で積まれるコマンドの『戻し先』が別のBTになる。
    //   Ctrl+Z を押すとこのエディタに前のグラフが載り、そのまま保存すると
    //   今開いているファイルが前のBTで上書きされる。
    //   （セッション最初の1本では既定構築の null が入っており、
    //     RestoreGraphState の contains ガードに弾かれて「最初のUndoだけ無反応」になる）
    m_lastSavedState = GetGraphStateAsJson();
    m_dirty          = false;

    CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフをロードしました: %s", path.c_str());
}

// ====================================================================
// 選択されたノードと、その間のリンクをまとめて複製する高度な機能
// ====================================================================
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
