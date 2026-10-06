#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Editor/Core/EditorCommandHistory.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
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
        m_dirty = true; // グラフ変更はすべてここを通る
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
    ed::SetCurrentEditor(m_nodeContext);

    // 描画領域が極小のフレームは中身を描かない（ドッキング操作中にサイズが 0 付近まで潰れる）
    ImVec2 availableSize = ImGui::GetContentRegionAvail();
    if (availableSize.x < 50.0f || availableSize.y < 50.0f)
    {
        ImGui::Dummy(ImVec2(50.0f, 50.0f));
        return;
    }

    ZoneScopedN("BTW: DrawContents");

    // サブツリーへ入る予約を消化する（前フレームのダブルクリックなど）。
    //   描画の途中で階層を変えると、同じフレームの残りの処理が別の階層を触るので、フレームの頭でやる。
    //   外側への道のりは graphId を辿って作るので、アウトラインから深い階層へ直接飛ぶこともできる。
    if (m_pendingEnterId != 0)
    {
        std::vector<uint32_t> path;
        for (uint32_t g = m_pendingEnterId; g != 0 && path.size() <= m_nodes.size();)
        {
            const EditorBTNode *n = FindNode(g);
            if (!n || n->type != EditorBTNodeType::SubTree)
            {
                path.clear();
                break;
            }
            path.insert(path.begin(), g);
            g = n->graphId;
        }
        if (!path.empty())
        {
            m_viewPath = std::move(path);
            ed::ClearSelection();
            m_needInitialZoom = true; // 入った先の全体へ視点を合わせる
            m_framesSinceOpen = 0;
        }
        m_pendingEnterId = 0;
    }

    // Undo などで、潜っている SubTree が消えたら（または別の場所へ移ったら）残っている深さまで戻る
    for (size_t i = 0; i < m_viewPath.size(); ++i)
    {
        const EditorBTNode *n      = FindNode(m_viewPath[i]);
        const uint32_t      parent = (i == 0) ? 0 : m_viewPath[i - 1];
        if (!n || n->type != EditorBTNodeType::SubTree || n->graphId != parent)
        {
            m_viewPath.resize(i);
            break;
        }
    }

    // 親子関係と「隠れているノード（別の階層）」を、どのペインより先に確定させる。
    //   アウトライン・キャンバス・インスペクタが同じフレームで同じ見え方を前提にできる。
    RebuildHierarchy();

    // 隠れたノードの選択は外す。見えないものがインスペクタに出続けたり、
    // Delete キーで見えないまま消えたりするのを防ぐ。
    {
        ZoneScopedN("BTW: DeselectHidden");
        ZoneValue(m_hiddenNodes.size());
        for (uint32_t id : m_hiddenNodes)
        {
            if (ed::IsNodeSelected(id)) ed::DeselectNode(id);
        }
    }

    const BehaviorTreeComponent *activeAI = FindActiveAI(context);

    // 実行中の表示は、その AI が動かしている木を開いているときだけ出す。
    //   状態は Bake 後の添字で対応付けるので、別のファイルに当てると無関係なノードが光る。
    if (activeAI && activeAI->assetPath != m_currentPath) activeAI = nullptr;

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
    if (ImGui::Button("保存 ( Save )"))
    {
        // 開いているファイル名を初期値にする。潜ったサブツリーを直して保存するとき、
        //   毎回ファイル名を打ち直さずに済む。
        const std::string defaultName = m_currentPath.empty() ? "BossAI.json" : FileNameOf(m_currentPath);
        auto picked = FileDialog::SavePreset("BehaviorTreeNode", "Save Behavior Tree", defaultName);
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
        ImGui::SetTooltip("木の構造を字下げ付きの文章にして書き出します。\n"
                          "ノードの意味の説明も先頭に付くので、そのまま AI に渡して組み方を相談できます。");
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

    DrawBreadcrumb();
}

void BehaviorTreeWindow::DrawBreadcrumb()
{
    // 一番外にいるときは出さない（押す先が無い）
    if (m_viewPath.empty()) return;

    auto jumpTo = [&](size_t depth) {
        m_viewPath.resize(depth);
        m_needInitialZoom = true; // 戻った先の全体へ視点を合わせる
        m_framesSinceOpen = 0;
    };

    if (ImGui::Button("一番外")) jumpTo(0);
    for (size_t i = 0; i < m_viewPath.size(); ++i)
    {
        const EditorBTNode *sub = FindNode(m_viewPath[i]);
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        // 今いる段（末尾）は押しても何も起きないので、押せない見た目にする
        const bool isLast = (i + 1 == m_viewPath.size());
        if (isLast) ImGui::BeginDisabled();
        const bool clicked = ImGui::Button(sub ? GetDisplayName(*sub).c_str() : "?");
        if (isLast) ImGui::EndDisabled();
        ImGui::PopID();
        if (clicked)
        {
            jumpTo(i + 1);
            break;
        }
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

    // 葉（アクション/条件/サブツリー）とグループ以外は出力ピンを持つ
    if (type != EditorBTNodeType::Condition && type != EditorBTNodeType::Action &&
        type != EditorBTNodeType::SubTree && type != EditorBTNodeType::Group)
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
    node.graphId             = CurrentGraphId();
    m_nodes.push_back(node);
    return node.id;
}

uint32_t BehaviorTreeWindow::CreateSubTreeNode(float x, float y)
{
    CreateNode(EditorBTNodeType::SubTree, "サブツリー", x, y);
    const uint32_t subId = m_nodes.back().id;

    // 中の入口。Bake はここから繋がった木を SubTree の位置へ差し込む
    CreateNode(EditorBTNodeType::Root, "入口", 0.0f, 0.0f);
    m_nodes.back().graphId = subId;
    return subId;
}

std::vector<uint32_t> BehaviorTreeWindow::CollectSubTreeContents(uint32_t subTreeId) const
{
    // 入れ子のサブツリーの中身も含める。外側から1段ずつ広げる
    std::vector<uint32_t>        result;
    std::unordered_set<uint32_t> owners = {subTreeId};
    for (bool grew = true; grew;)
    {
        grew = false;
        for (const auto &n : m_nodes)
        {
            if (!owners.count(n.graphId) || owners.count(n.id)) continue;
            result.push_back(n.id);
            owners.insert(n.id); // SubTree でなければ中身を持たないので、入れても害は無い
            grew = true;
        }
    }
    return result;
}

EditorBTNode *BehaviorTreeWindow::FindSubTreeEntry(uint32_t subTreeId)
{
    for (auto &n : m_nodes)
        if (n.graphId == subTreeId && n.type == EditorBTNodeType::Root) return &n;
    return nullptr;
}

void BehaviorTreeWindow::DeleteNodeAndLinks(uint32_t nodeId)
{
    // SubTree を消すときは中身も消す。残すと、どの階層からも見えないノードがファイルに溜まり続ける。
    std::vector<uint32_t> targets = CollectSubTreeContents(nodeId);
    targets.push_back(nodeId);

    for (uint32_t id : targets)
    {
        auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const EditorBTNode &n) { return n.id == id; });
        if (it == m_nodes.end()) continue;

        // このノードのピンに繋がっているリンクを消してから、本体を消す
        const uint32_t inPinId  = it->inputPin.id;
        const uint32_t outPinId = it->outputPin.id;
        m_links.erase(std::remove_if(m_links.begin(), m_links.end(),
                                     [inPinId, outPinId](const BTLink &link) {
                                         return link.startPinId == outPinId || link.endPinId == inPinId;
                                     }),
                      m_links.end());
        m_nodes.erase(it);
    }

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

    // 消すのは選んだノードだけ（SubTree の中身は DeleteNodeAndLinks が一緒に消す）。
    //   外側のノードは、JSON の中で所属先がコピーに含まれていないもの
    std::unordered_set<uint32_t> copied;
    for (const auto &jn : clip["nodes"])
        copied.insert(jn["id"].get<uint32_t>());
    for (const auto &jn : clip["nodes"])
    {
        if (copied.count(jn.value("graphId", 0u))) continue;
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

    // 選んだノード。ROOT（一番外の始点・サブツリーの入口）は1つの階層に1つだけなので運ばない
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

    // SubTree の中身も入れる
    for (uint32_t id : std::vector<uint32_t>(sources))
    {
        for (uint32_t inner : CollectSubTreeContents(id))
            if (picked.insert(inner).second) sources.push_back(inner);
    }

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

        // 貼る位置の基準は、今の階層にいる（選んだ）ノードの左上
        if (n->graphId == CurrentGraphId())
        {
            minX = (std::min)(minX, jn["posX"].get<float>());
            minY = (std::min)(minY, jn["posY"].get<float>());
        }
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

    // 古いIDから新しいIDへの対応（リンクの繋ぎ直しと、中身の所属の付け替えに使う）
    std::map<uint32_t, uint32_t> oldToNewNodeId;
    std::map<uint32_t, uint32_t> oldToNewPinId;

    std::vector<EditorBTNode> newNodes;
    for (const auto &jn : clip["nodes"])
    {
        EditorBTNode node = NodeFromJson(jn); // 行動・条件は保存キーで引き直すので、別ファイルからでも正しく戻る

        const uint32_t oldId  = node.id;
        node.id               = GetNextId();
        oldToNewNodeId[oldId] = node.id;

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
        // 所属先がコピーに含まれていれば、SubTree の中身。新しい SubTree の中へ付け替え、位置はそのまま
        auto g = oldToNewNodeId.find(n.graphId);
        if (g != oldToNewNodeId.end())
        {
            n.graphId = g->second;
            continue;
        }

        // 選んでいたノード。今見ている階層へ置き、選択範囲の左上が anchor に来るようにずらす
        n.graphId = CurrentGraphId();
        n.posX    = anchor.x + (n.posX - originX);
        n.posY    = anchor.y + (n.posY - originY);
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

void BehaviorTreeWindow::CollapseSelectionToSubTree()
{
    std::vector<ed::NodeId> selected(static_cast<size_t>(ed::GetSelectedObjectCount()));
    selected.resize(static_cast<size_t>(ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()))));

    std::unordered_set<uint32_t> picked;
    for (auto sid : selected)
    {
        const uint32_t      id = ToEditorId(sid.Get());
        const EditorBTNode *n  = FindNode(id);
        if (!n || n->type == EditorBTNodeType::Group || n->type == EditorBTNodeType::Root) continue;
        picked.insert(id);
    }

    // 選択の中で一番上のノード（親が選択に入っていないもの）。これが1つでなければまとめられない
    uint32_t top      = 0;
    int      topCount = 0;
    for (uint32_t id : picked)
    {
        auto p = m_parentOf.find(id);
        if (p != m_parentOf.end() && picked.count(p->second)) continue;
        top = id;
        ++topCount;
    }
    if (topCount != 1)
    {
        CCL_LOG_WARN(LogCategory::Editor,
                     "[BT] サブツリーにまとめるには、根が1つの枝を選んでください（今の選択の根は %d 個）", topCount);
        return;
    }

    // まとめるのは根とその子孫すべて。子孫を残すと、中と外をまたぐ線ができてしまう
    std::vector<uint32_t> members = {top};
    for (size_t i = 0; i < members.size(); ++i)
    {
        auto c = m_childrenOf.find(members[i]);
        if (c == m_childrenOf.end()) continue;
        for (uint32_t child : c->second)
            if (std::find(members.begin(), members.end(), child) == members.end()) members.push_back(child);
    }

    const EditorBTNode *topNode = FindNode(top);
    const float         topX    = topNode->posX;
    const float         topY    = topNode->posY;
    const uint32_t      topIn   = topNode->inputPin.id;
    const float         weight  = topNode->weight;
    const std::string   name    = GetDisplayName(*topNode);

    // 根の位置に SubTree を置く。同じ X に置くので、兄弟の中での実行順は変わらない
    const uint32_t subId = CreateSubTreeNode(topX, topY); // m_nodes が伸びるので、ここ以降 topNode は使わない
    FindNode(subId)->name = name;
    // 重み付きセレクターの下なら、抽選の重みは外から見える SubTree が引き継ぐ（Bake も SubTree の重みを使う）
    FindNode(subId)->weight = weight;
    FindNode(top)->weight   = 1.0f;

    EditorBTNode *entry = FindSubTreeEntry(subId);
    entry->posX         = topX;
    entry->posY         = topY - 150.0f; // 入口は根の真上
    const uint32_t entryOut = entry->outputPin.id;
    const uint32_t subIn    = FindNode(subId)->inputPin.id;

    for (uint32_t id : members)
        FindNode(id)->graphId = subId;

    // 外の親からの線は SubTree へ付け替え、中では入口から根へ繋ぐ
    for (auto &link : m_links)
        if (link.endPinId == topIn) link.endPinId = subIn;
    m_links.push_back({GetNextId(), entryOut, topIn});

    ed::ClearSelection();
    RecordStateForUndo("Collapse To SubTree");
    CCL_LOG_INFO(LogCategory::Editor, "[BT] %zu ノードをサブツリー「%s」にまとめました", members.size(), name.c_str());
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
    case EditorBTNodeType::SubTree: return ImColor(115, 150, 35);         // 黄緑（85°）：金と緑の間の空き
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

