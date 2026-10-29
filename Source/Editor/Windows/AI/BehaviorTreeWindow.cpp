#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/AI/BehaviorTreeEditorCommon.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeComponents.h"
#include "Game/Logics/AI/Drone/Formation/FormationRegistry.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/Logger.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>

using namespace BTEditor;

// ---------------------------------------------------------------------------
// 生成と破棄
// ---------------------------------------------------------------------------

// グラフの変更1回分。前後の状態を丸ごと持ち、Undo/Redo で差し替える
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

    // 背景のグリッドを大きく・濃くする。既定は 32 間隔・透明度 40/255 とかなり細かく薄い。
    //   しかも透明度に「倍率の2乗」が掛かるので、全体を収めるために縮小していると事実上見えない。
    //   間隔の GridSize はライブラリに足した項目（imgui_node_editor.h）。
    ed::SetCurrentEditor(m_nodeContext);
    ed::GetStyle().GridSize                    = 64.0f;
    ed::GetStyle().Colors[ed::StyleColor_Grid] = ImColor(140, 140, 150, 130);
    ed::SetCurrentEditor(nullptr);

    CreateNode(EditorBTNodeType::Root, "ROOT", 0.0f, 0.0f);
}

BehaviorTreeWindow::~BehaviorTreeWindow()
{
    if (m_nodeContext) ed::DestroyEditor(m_nodeContext);
}

// ---------------------------------------------------------------------------
// Undo の記録
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::RecordStateForUndo(const std::string &commandName)
{
    // グラフ全体を JSON にして比べるので重い。毎フレーム出ていたら、どこかが毎フレーム呼んでいる。
    ZoneScopedN("BTW: RecordStateForUndo");

    nlohmann::json newState = GetGraphStateAsJson();
    // 状態が変わっていればコマンドをスタックに積む
    if (m_lastSavedState != newState)
    {
        // グラフ変更はすべてここを通るので、未保存の印もここで立てる。
        // ノードを「動かしただけ」はここを通らないので検出しない。誤検出で毎回警告が出ると
        // 警告を読まなくなるので、取りこぼす側に倒してある。
        m_dirty = true;
        EditorCommandHistory::Instance().ExecuteCommand(
            std::make_unique<BTGraphChangeCommand>(this, commandName, m_lastSavedState, newState));
        m_lastSavedState = newState;
    }
}

// ---------------------------------------------------------------------------
// ウィンドウ枠
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::DrawContents(EditorContext &context)
{
    // データの技の名前を引く先。陣形の実体は World ごとにあるので毎フレーム引き直す（FormationRegistry.h）。
    m_formations = context.world ? CCL::AI::Formation::FormationRegistry::Find(*context.world) : nullptr;

    ed::SetCurrentEditor(m_nodeContext);

    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    if (availableSize.x < 50.0f || availableSize.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    ZoneScopedN("BTW: DrawContents");

    // 親子関係を、どのペインより先に確定させる。
    //   ヒエラルキー・キャンバス・インスペクタが同じフレームで同じ並びを前提にできる。
    RebuildHierarchy();

    const BehaviorTreeComponent *activeAI = FindActiveAI(context);

    // 実行中の表示は、その AI が動かしている木を開いているときだけ出す。
    //   状態は Bake 後の添字で対応付けるので、別のファイルに当てると無関係なノードが光る。
    if (activeAI && activeAI->assetPath != m_currentPath) activeAI = nullptr;

    if (ImGui::BeginTable("BTEditorTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, availableSize))
    {
        // 初期幅の比率（ヒエラルキー18% / ワークスペース60% / インスペクタ22%）
        ImGui::TableSetupColumn("Outline", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Workspace", ImGuiTableColumnFlags_WidthStretch, 0.60f);
        ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableNextRow();

        // --- 左ペイン (ヒエラルキー) ---
        ImGui::TableSetColumnIndex(0);
        DrawOutliner(activeAI);

        // --- 中央ペイン (ツールバー ＋ ノードエディタ) ---
        ImGui::TableSetColumnIndex(1);
        {
            ZoneScopedN("BTW: Toolbar");
            DrawToolbar();
        }
        DrawNodeWorkspace(activeAI);

        // --- 右ペイン (インスペクタ) ---
        ImGui::TableSetColumnIndex(2);
        DrawInspector();

        ZoneScopedN("BTW: EndTable");
        ImGui::EndTable();
    }
}

void BehaviorTreeWindow::DrawToolbar()
{
    // 処理フロー:
    // 1. ファイル操作（保存・開く・テキスト書き出し）
    // 2. 未保存の表示と、開く前の確認
    // 3. 編集の補助（整列・囲む・自動追従）

    // ---------------------------------------------------------
    // 1. ファイル操作
    // ---------------------------------------------------------
    if (ImGui::Button("保存"))
    {
        // 開いているファイル名を初期値にする。上書き保存のとき、毎回ファイル名を打ち直さずに済む。
        const std::string defaultName = m_currentPath.empty() ? "BossAI.json" : FileNameOf(m_currentPath);
        auto picked = FileDialog::SavePreset("BehaviorTreeNode", "Save Behavior Tree", defaultName);
        if (picked)
        {
            std::string rel = FileDialog::ConvertToRelativePath(*picked);
            SaveGraph(rel);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("開く"))
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
    ImGui::SameLine();
    if (ImGui::Button("テキスト書き出し"))
    {
        // 既定のファイル名は「開いている BT の名前_Outline.txt」（BossCombat.json → BossCombat_Outline.txt）
        std::string base = m_currentPath.empty() ? "BehaviorTree" : FileNameOf(m_currentPath);
        if (base.ends_with(".json")) base.resize(base.size() - 5);

        auto picked = FileDialog::Save({.title           = "Export Behavior Tree as Text",
                                        .contextId       = "BehaviorTreeOutline", // 前回選んだフォルダを覚えておく用
                                        .filters         = {{"Text", "*.txt"}},
                                        .defaultFileName = base + "_Outline.txt",
                                        .defaultExt      = "txt"});
        if (picked) ExportOutlineText(*picked);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("木の構造をインデント付きのテキストで書き出す（先頭に凡例付き）");
    }

    // ---------------------------------------------------------
    // 2. 未保存
    // ---------------------------------------------------------

    // ウィンドウ名に "*" を足さずツールバー側に出す。ImGui はウィンドウ名を ID に使っているため、
    // 名前を変えるとドッキング配置が別物として扱われる。
    if (m_dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "未保存");
    }

    // 未保存のまま「開く」を押したときの確認。
    if (!m_pendingOpenPath.empty()) ImGui::OpenPopup("BTOpenConfirm");
    if (ImGui::BeginPopupModal("BTOpenConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("未保存の変更がある");
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

    // ---------------------------------------------------------
    // 3. 編集の補助
    // ---------------------------------------------------------
    ImGui::Separator();

    if (ImGui::Button("整列"))
        m_layoutRequested = true; // 実行はキャンバス描画の頭まで持ち越す
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("全ノードを木の形に並べ直す。\n"
                          "子の左右の順（実行順）は変えない。コメント枠も追従する。");
    }

    ImGui::SameLine();
    if (ImGui::Button("選択を囲む (Ctrl+G)"))
        GroupSelectedNodes();
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("選択中のノードを囲むコメント枠を作る");
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::Checkbox("自動追従", &m_autoFollow);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("実行中のノードへ表示を自動で寄せる");
    }
}

// ---------------------------------------------------------------------------
// 編集操作
// ---------------------------------------------------------------------------

uint32_t BehaviorTreeWindow::CreateNode(EditorBTNodeType type, const std::string &name, float x, float y)
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

    // 葉（アクション/条件）とグループ以外は出力ピンを持つ
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
    return node.id;
}

void BehaviorTreeWindow::DeleteNodeAndLinks(uint32_t nodeId)
{
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode &n) { return n.id == nodeId; });
    if (it == m_nodes.end()) return;

    // このノードのピンに繋がっているリンクを消してから、本体を消す
    const uint32_t inPinId  = it->inputPin.id;
    const uint32_t outPinId = it->outputPin.id;
    m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                 [inPinId, outPinId](const BTLink &link) {
                                     return link.startPinId == outPinId || link.endPinId == inPinId;
                                 }),
                  m_links.end());
    m_nodes.erase(it);

    CCL_LOG_INFO(LogCategory::Editor, "Node(ID:%u) and its connected links were deleted.", nodeId);
}

void BehaviorTreeWindow::DuplicateSelectedNodes()
{
    // コピーして右下へずらして貼る。クリップボードは通さない（Ctrl+C で取っておいた内容を消さないため）
    const nlohmann::json clip = CopySelectionToJson();
    if (clip.is_null()) return;

    PasteFromJson(clip, ImVec2(clip.value("originX", 0.0f) + 50.0f, clip.value("originY", 0.0f) + 50.0f));
    RecordStateForUndo("Duplicate Nodes");
}

namespace
{
    // クリップボードの JSON の目印。関係ない文字列（他アプリのコピー）を貼ろうとしても無視できるように
    constexpr const char *kClipboardFormat = "ProgressCore.BehaviorTree.Clipboard";
} // namespace

void BehaviorTreeWindow::CopySelection()
{
    const nlohmann::json clip = CopySelectionToJson();
    if (clip.is_null()) return;

    ImGui::SetClipboardText(clip.dump().c_str());
    CCL_LOG_INFO(LogCategory::Editor, "[BT] %zu ノードをコピーしました", clip["nodes"].size());
}

void BehaviorTreeWindow::CutSelection()
{
    const nlohmann::json clip = CopySelectionToJson();
    if (clip.is_null()) return;
    ImGui::SetClipboardText(clip.dump().c_str());

    for (const auto &jn : clip["nodes"])
    {
        DeleteNodeAndLinks(jn["id"].get<uint32_t>());
    }

    ed::ClearSelection();
    RecordStateForUndo("Cut Nodes");
}

void BehaviorTreeWindow::PasteClipboard(ImVec2 canvasPos)
{
    const char *text = ImGui::GetClipboardText();
    if (!text || !*text) return;

    // 例外を投げない形で読む。他アプリのコピーが入っているのは普通のことなので、ログも出さない
    const nlohmann::json clip = nlohmann::json::parse(text, nullptr, false);
    if (!clip.is_object() || clip.value("format", "") != kClipboardFormat) return;

    PasteFromJson(clip, canvasPos);
    RecordStateForUndo("Paste Nodes");
}

nlohmann::json BehaviorTreeWindow::CopySelectionToJson()
{
    std::vector<ed::NodeId> selected(static_cast<size_t>(ed::GetSelectedObjectCount()));
    selected.resize(static_cast<size_t>(ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()))));

    // 選んだノード。ROOT は1つのグラフに1つだけなので運ばない
    std::vector<uint32_t>        sources;
    std::unordered_set<uint32_t> picked;
    for (auto sid : selected)
    {
        const uint32_t      id = ToEditorId(sid.Get());
        const EditorBTNode *n  = FindNode(id);
        if (!n || n->type == EditorBTNodeType::Root) continue;
        if (picked.insert(id).second) sources.push_back(id);
    }
    if (sources.empty()) return nullptr;

    nlohmann::json clip;
    clip["format"] = kClipboardFormat;
    clip["nodes"]  = nlohmann::json::array();

    std::unordered_set<uint32_t> pins;
    float                        minX = FLT_MAX, minY = FLT_MAX;
    for (uint32_t id : sources)
    {
        const EditorBTNode *n = FindNode(id);
        if (!n) continue;

        nlohmann::json jn = NodeToJson(*n);
        clip["nodes"].push_back(jn);
        if (n->inputPin.id != 0) pins.insert(n->inputPin.id);
        if (n->outputPin.id != 0) pins.insert(n->outputPin.id);

        // 貼る位置の基準は、選んだノード全体の左上
        minX = (std::min)(minX, jn["posX"].get<float>());
        minY = (std::min)(minY, jn["posY"].get<float>());
    }
    clip["originX"] = (minX == FLT_MAX) ? 0.0f : minX;
    clip["originY"] = (minY == FLT_MAX) ? 0.0f : minY;

    clip["links"] = nlohmann::json::array();
    for (const auto &link : m_links)
    {
        if (pins.count(link.startPinId) && pins.count(link.endPinId))
            clip["links"].push_back({{"startPin", link.startPinId}, {"endPin", link.endPinId}});
    }
    return clip;
}

void BehaviorTreeWindow::PasteFromJson(const nlohmann::json &clip, ImVec2 anchor)
{
    const float originX = clip.value("originX", 0.0f);
    const float originY = clip.value("originY", 0.0f);

    // 古いピンIDから新しいピンIDへの対応（リンクの繋ぎ直しに使う）
    std::map<uint32_t, uint32_t> oldToNewPinId;

    std::vector<EditorBTNode> newNodes;
    for (const auto &jn : clip["nodes"])
    {
        EditorBTNode node = NodeFromJson(jn); // 行動・条件は保存キーで引き直すので、別ファイルからでも正しく戻る
        node.id           = GetNextId();

        if (node.inputPin.id != 0)
        {
            const uint32_t newIn            = GetNextId();
            oldToNewPinId[node.inputPin.id] = newIn;
            node.inputPin                   = {newIn, node.id, true};
        }
        if (node.outputPin.id != 0)
        {
            const uint32_t newOut            = GetNextId();
            oldToNewPinId[node.outputPin.id] = newOut;
            node.outputPin                   = {newOut, node.id, false};
        }
        newNodes.push_back(node);
    }

    m_pendingSelectIds.clear();
    for (auto &n : newNodes)
    {
        // 選択範囲の左上が anchor に来るようにずらす
        n.posX = anchor.x + (n.posX - originX);
        n.posY = anchor.y + (n.posY - originY);
        m_pendingSelectIds.push_back(n.id);
    }

    std::vector<BTLink> newLinks;
    for (const auto &jl : clip["links"])
    {
        auto s = oldToNewPinId.find(jl["startPin"].get<uint32_t>());
        auto e = oldToNewPinId.find(jl["endPin"].get<uint32_t>());
        if (s == oldToNewPinId.end() || e == oldToNewPinId.end()) continue;
        newLinks.push_back({GetNextId(), s->second, e->second});
    }

    m_nodes.insert(m_nodes.end(), newNodes.begin(), newNodes.end());
    m_links.insert(m_links.end(), newLinks.begin(), newLinks.end());

    ed::ClearSelection(); // 貼ったものは次のフレームで選択する（m_pendingSelectIds）
    CCL_LOG_INFO(LogCategory::Editor, "[BT] %zu ノードを貼り付けました", newNodes.size());
}

EditorBTNode *BehaviorTreeWindow::FindNode(uint32_t nodeId)
{
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [nodeId](const EditorBTNode &n) { return n.id == nodeId; });
    return (it == m_nodes.end()) ? nullptr : &*it;
}

// ---------------------------------------------------------------------------
// 表示名・色・実行中の状態
// ---------------------------------------------------------------------------

const BehaviorTreeComponent *BehaviorTreeWindow::FindActiveAI(EditorContext &context)
{
    if (context.selectedEntity == CCL::ECS::InvalidEntityID || !context.world) return nullptr;
    return context.world->GetComponent<BehaviorTreeComponent>(context.selectedEntity);
}

ImColor BehaviorTreeWindow::HeaderColorOf(EditorBTNodeType type)
{
    // 9種類の色相を 40° 以上ずつ離して、似た色が隣り合わないようにしてある。
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
    case EditorBTNodeType::UtilitySelector: return ImColor(115, 150, 35); // 黄緑（85°）：金と緑の間の空き
    default: return ImColor(100, 100, 100);
    }
}

const char *BehaviorTreeWindow::TypeNameOf(EditorBTNodeType type)
{
    // UE5 と同じく英語の種類名にする（Selector / Sequence は BT の用語として通じるため）
    switch (type)
    {
    case EditorBTNodeType::Root: return "Root";
    case EditorBTNodeType::Selector: return "Selector";
    case EditorBTNodeType::Sequence: return "Sequence";
    case EditorBTNodeType::Condition: return "Condition";
    case EditorBTNodeType::Action: return "Action";
    case EditorBTNodeType::Group: return "Comment";
    case EditorBTNodeType::Decorator: return "Decorator";
    case EditorBTNodeType::WeightedSelector: return "WeightedSelector";
    case EditorBTNodeType::Parallel: return "Parallel";
    case EditorBTNodeType::UtilitySelector: return "UtilitySelector";
    default: return "Unknown";
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

std::string BehaviorTreeWindow::GetDisplayName(const EditorBTNode &node) const
{
    // 処理フロー:
    // 1. まとまり（Selector など）とコメント枠：自分で付けた名前
    // 2. Condition：表示名の "N" を param の値に置き換える
    // 3. Action：行動名＋param（データの技は技の名前）
    // 4. Decorator：種類＋param

    // ---------------------------------------------------------
    // 1. まとまり・コメント枠
    // ---------------------------------------------------------
    if (UsesCustomName(node.type)) return node.name;

    char number[32];
    snprintf(number, sizeof(number), "%g", node.param);

    // ---------------------------------------------------------
    // 2. Condition
    // ---------------------------------------------------------

    // 登録表の表示名は「距離が N m 未満」の形なので、値を入れるとそのまま読める文になる
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

    // ---------------------------------------------------------
    // 3. Action
    // ---------------------------------------------------------
    if (node.type == EditorBTNodeType::Action)
    {
        if (node.actionOrConditionId == A_DroneFormation)
            return GetDroneFormationLabel(node.param);
        if (node.actionOrConditionId == A_DroneTechnique)
            return GetDroneTechniqueLabel(node.param);

        // 末尾の英語の併記「(Move To Player)」は外す。一覧が横に長くなりすぎるため
        std::string  name  = GetActionDisplayName(node.actionOrConditionId);
        const size_t paren = name.rfind(" (");
        if (paren != std::string::npos && name.back() == ')')
            name.erase(paren);

        if (const char *label = GetActionParamLabel(node.actionOrConditionId))
            return name + "（" + FormatParam(label, node.param) + "）";
        return name;
    }

    // ---------------------------------------------------------
    // 4. Decorator
    // ---------------------------------------------------------
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

const char *BehaviorTreeWindow::GetDroneFormationLabel(float slotParam) const
{
    static thread_local char buf[96];

    const int slot = static_cast<int>(std::lround(slotParam));
    if (slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount))
    {
        snprintf(buf, sizeof(buf), "ドローン：データの技（技が選ばれていません）");
        return buf;
    }

    const auto  type = static_cast<DroneFormationType>(slot);
    const char *name = m_formations ? m_formations->DataFormationName(type) : nullptr;

    // 名前が無い＝その枠に陣形が入っていない（またはシーンが無い）。指しても静止するので、そう見えるように出す。
    if (!name)
        snprintf(buf, sizeof(buf), "ドローン：枠%d に陣形がありません", slot);
    else
        snprintf(buf, sizeof(buf), "ドローン：%s  (枠%d)", name, slot);
    return buf;
}

const char *BehaviorTreeWindow::GetDroneTechniqueLabel(float slotParam) const
{
    static thread_local char buf[96];

    const int slot = static_cast<int>(std::lround(slotParam));
    if (const char *name = DroneTechniqueName(slot))
        snprintf(buf, sizeof(buf), "ドローンの技：%s", name);
    else if (slot <= 0)
        snprintf(buf, sizeof(buf), "ドローンの技（技が選ばれていません）");
    else
        snprintf(buf, sizeof(buf), "ドローンの技：枠%d に技がありません", slot); // 指しても失敗するだけ
    return buf;
}

const char *BehaviorTreeWindow::DroneTechniqueName(int slot) const
{
    if (!m_formations || slot <= 0 || slot >= static_cast<int>(kDroneFormationSlotCount)) return nullptr;

    const CCL::AI::Formation::IFormation *formation = m_formations->Get(static_cast<DroneFormationType>(slot));
    CCL::AI::Formation::TechniqueTiming   timing;
    if (!formation || !formation->TryGetTiming(timing)) return nullptr;

    // データの技は陣形エディタで付けた名前、C++ の技は陣形の一覧の表示名
    if (const char *name = formation->DisplayName()) return name;
    return kDroneFormationNames[static_cast<size_t>(slot)];
}

