// BehaviorTreeWindow：保存・読込・Bake と、親子関係（実行順・表示する階層）の計算。
//
// 描画はしない。ノードエディタのライブラリに触れるのは座標の読み出し（NodeToJson）だけ。
//
// Bake の形式:
//   幅優先の順でフラット配列にし、各ノードの "children" に子の添字を入れる。ランタイムは
//   この配列だけを読むので、エディタ側の構造（ピン・リンク・コメント枠）は自由に変えてよい。
//   サブツリーもエディタだけの構造で、SubTree の場所へ中身（入口の子）をそのまま差し込んで平らにする。
#include "BehaviorTreeWindow.h"
#include "BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <algorithm>
#include <functional>
#include <queue>

using namespace BTEditor;

// ====================================================================
// グラフ ⇔ JSON（保存・読込・Undo の全経路がここを通る）
// ====================================================================
void BehaviorTreeWindow::WriteKeyAndParam(nlohmann::json &j, const EditorBTNode &node)
{
    // 行動/条件は数値IDでなく名前（保存キー）で書く。IDは追加のたびに増えるだけの内部都合で、
    // JSON を開いた人には読めない。Selector などに "action": "None" が並ばないよう、意味のある種類にだけ書く。
    if (node.type == EditorBTNodeType::Condition || node.type == EditorBTNodeType::Action)
    {
        const bool         isCondition = (node.type == EditorBTNodeType::Condition);
        const ActionEntry *e           = isCondition ? FindConditionEntry(node.actionOrConditionId)
                                                     : FindActionEntry(node.actionOrConditionId);
        j[isCondition ? "condition" : "action"] = e ? e->key : "None";
    }

    if (node.type == EditorBTNodeType::Action || node.type == EditorBTNodeType::Decorator ||
        node.type == EditorBTNodeType::Condition)
        j["param"] = node.param;

    // 重みは既定(1.0)から動かしたときだけ。読込側の既定も 1.0 なので意味は同じ
    if (node.weight != 1.0f)
        j["weight"] = node.weight;
}

void BehaviorTreeWindow::RestoreActionId(EditorBTNode &node, const nlohmann::json &jn)
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

nlohmann::json BehaviorTreeWindow::NodeToJson(const EditorBTNode &node) const
{
    nlohmann::json jn;
    jn["id"]       = node.id;
    jn["type"]     = static_cast<int>(node.type);
    jn["actionId"] = static_cast<int>(node.actionOrConditionId);
    if (UsesCustomName(node.type)) jn["name"] = node.name; // Action などの名前は中身から作るので保存しない
    WriteKeyAndParam(jn, node);

    // ピンのID。リンクはピンIDで繋がっているので、これが無いとリンクを復元できない。
    jn["inputPinId"]  = node.inputPin.id;
    jn["outputPinId"] = node.outputPin.id;

    // 描画前のノード（生成・整列・Undo の直後）は、ライブラリ側の座標がまだ古い。
    const ImVec2 pos = node.positionInitialized ? ed::GetNodePosition(node.id) : ImVec2(node.posX, node.posY);
    jn["posX"]       = pos.x;
    jn["posY"]       = pos.y;

    if (node.type == EditorBTNodeType::Group)
    {
        jn["width"]           = node.width;
        jn["height"]          = node.height;
        jn["groupColorIndex"] = node.groupColorIndex;
    }
    if (node.type == EditorBTNodeType::Decorator)
    {
        jn["decoratorType"] = node.decoratorType;
    }
    if (node.graphId != 0)
    {
        jn["graphId"] = node.graphId; // サブツリーの中にいるときだけ書く（一番外は省く）
    }
    return jn;
}

EditorBTNode BehaviorTreeWindow::NodeFromJson(const nlohmann::json &jn)
{
    EditorBTNode node;
    node.id   = jn["id"];
    node.type = static_cast<EditorBTNodeType>(jn["type"].get<int>());
    // 古いファイルの Action などには "name" が残っているが、表示に使わないので読まない
    if (UsesCustomName(node.type)) node.name = jn.value("name", "");
    RestoreActionId(node, jn);
    node.param   = jn.value("param", 0.0f);
    node.weight  = jn.value("weight", 1.0f);
    node.posX    = jn.value("posX", 0.0f);
    node.posY    = jn.value("posY", 0.0f);
    node.graphId = jn.value("graphId", 0u);

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
    // 保存されたIDをそのまま使う（GetNextId で振り直すとリンクが切れる）
    node.inputPin  = {jn.value("inputPinId", 0u), node.id, true};
    node.outputPin = {jn.value("outputPinId", 0u), node.id, false};

    // 座標をキャンバスへ再適用させる
    node.positionInitialized = false;
    return node;
}

nlohmann::json BehaviorTreeWindow::GetGraphStateAsJson()
{
    if (m_nodeContext) ed::SetCurrentEditor(m_nodeContext);

    nlohmann::json root;
    root["editor_nodes"] = nlohmann::json::array();
    for (const auto &node : m_nodes)
        root["editor_nodes"].push_back(NodeToJson(node));

    root["editor_links"] = nlohmann::json::array();
    for (const auto &link : m_links)
        root["editor_links"].push_back({{"id", link.id}, {"startPin", link.startPinId}, {"endPin", link.endPinId}});

    root["last_id_counter"] = m_idCounter;
    return root;
}

void BehaviorTreeWindow::ApplyGraphJson(const nlohmann::json &state)
{
    m_nodes.clear();
    m_links.clear();
    m_idCounter = state.value("last_id_counter", 1);

    for (const auto &jn : state["editor_nodes"])
        m_nodes.push_back(NodeFromJson(jn));

    if (state.contains("editor_links"))
    {
        for (const auto &jl : state["editor_links"])
            m_links.push_back({jl["id"].get<uint32_t>(), jl["startPin"].get<uint32_t>(), jl["endPin"].get<uint32_t>()});
    }

    // 空回しの Bake で runtimeFlatIndex を振る。保存しなくても、読込・Undo の直後から
    // 実行中の状態表示（デバッグ）が正しいノードに出る。
    BuildBakedNodes();
}

void BehaviorTreeWindow::RestoreGraphState(const nlohmann::json &state)
{
    if (!state.contains("editor_nodes")) return;

    // Undo の前後で選択を保つ（インスペクタで見ていたノードが外れないように）
    std::vector<ed::NodeId> selectedNodes;
    if (m_nodeContext)
    {
        ed::SetCurrentEditor(m_nodeContext);
        selectedNodes.resize(ed::GetSelectedObjectCount());
        selectedNodes.resize(ed::GetSelectedNodes(selectedNodes.data(), static_cast<int>(selectedNodes.size())));
        ed::ClearSelection(); // 消えるノードを選択したまま作り直すと落ちる
    }

    ApplyGraphJson(state);

    for (auto id : selectedNodes)
        ed::SelectNode(id, true);
}

// ====================================================================
// ファイル
// ====================================================================
void BehaviorTreeWindow::SaveGraph(const std::string &path)
{
    ed::SetCurrentEditor(m_nodeContext);

    nlohmann::json root = GetGraphStateAsJson();
    root["nodes"]       = BuildBakedNodes();
    root["assetId"]     = 1;

    if (FileSystem::WriteAllText(path, root.dump(4)))
    {
        m_dirty       = false;
        m_currentPath = path;
        CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフを保存＆Bakeしました: %s", path.c_str());
    }
    else
    {
        // 保存できたつもりで編集を続けるのが一番まずいので、必ず出す
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフの保存に失敗しました: %s", path.c_str());
    }
}

bool BehaviorTreeWindow::LoadGraph(const std::string &path)
{
    const std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty())
    {
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフを読めませんでした: %s", path.c_str());
        return false;
    }

    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(jsonStr);
    }
    catch (...)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "AIグラフのパースに失敗しました: %s", path.c_str());
        return false;
    }
    if (!j.contains("editor_nodes")) return false;

    ed::SetCurrentEditor(m_nodeContext);
    ed::ClearSelection(); // 前のグラフのノードIDが選択に残らないように
    ApplyGraphJson(j);

    m_viewPath.clear(); // 前のグラフのサブツリーIDは、このグラフでは別のノードを指しうる
    m_pendingEnterId  = 0;
    m_needInitialZoom = true;

    // Undo の基準をこのグラフに引き直す。
    //   忘れると m_lastSavedState は「前に開いていたBT」のままで、読込後の最初の編集の
    //   『戻し先』が別のBTになる。Ctrl+Z → 保存で、今のファイルが前のBTで上書きされる。
    m_lastSavedState = GetGraphStateAsJson();
    m_dirty          = false;
    m_currentPath    = path;

    CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフをロードしました: %s", path.c_str());
    return true;
}

// ====================================================================
// Bake（ランタイム用のフラット配列）
// ====================================================================
nlohmann::json BehaviorTreeWindow::BuildBakedNodes()
{
    nlohmann::json jsonNodes = nlohmann::json::array();

    // 子の並び（＝実行順）は RebuildHierarchy が決める。読み込み直後など、
    // まだ今フレームの再構築を通っていない時にも呼ばれるので、ここで作り直す。
    RebuildHierarchy();

    // 木から外れたノードに古い添字が残ると、デバッグ表示で別ノードの状態が出る
    for (auto &n : m_nodes)
        n.runtimeFlatIndex = -1;

    // 一番外の ROOT から始める（サブツリーの入口も ROOT なので、階層で見分ける）
    auto rootIt = std::find_if(m_nodes.begin(), m_nodes.end(),
                               [](const EditorBTNode &n) { return n.type == EditorBTNodeType::Root && n.graphId == 0; });
    if (rootIt == m_nodes.end()) return jsonNodes;

    auto childrenOf = [&](uint32_t id) -> std::vector<uint32_t> {
        auto it = m_childrenOf.find(id);
        return (it == m_childrenOf.end()) ? std::vector<uint32_t>{} : it->second;
    };

    const std::vector<uint32_t> rootChildren = childrenOf(rootIt->id);
    if (rootChildren.empty()) return jsonNodes;

    if (rootChildren.size() > 1)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "【BTエラー】ROOTノードには複数の線はつなげません！必ず1つの「Selector」等につないでから分岐させてください。");
    }

    auto runtimeTypeOf = [](EditorBTNodeType type) {
        switch (type)
        {
        case EditorBTNodeType::Selector: return "Selector";
        case EditorBTNodeType::Sequence: return "Sequence";
        case EditorBTNodeType::Condition: return "Condition";
        case EditorBTNodeType::Action: return "Action";
        case EditorBTNodeType::Decorator: return "Decorator";
        case EditorBTNodeType::WeightedSelector: return "WeightedSelector";
        case EditorBTNodeType::Parallel: return "Parallel";
        default: return "Unknown"; // SubTree は bakeChild が中身へ置き換えるので、ここへは来ない
        }
    };

    // ランタイム用の1ノード分。ROOT 直下も子も必ずここを通す。
    //   以前は ROOT 直下だけ別に書いていて decoratorType が抜け、
    //   ROOT 直下の Cooldown が読み込み側の既定値 0（Inverter）として動いていた。
    auto bakeOne = [&](EditorBTNode &node) {
        nlohmann::json j;
        j["type"]                = runtimeTypeOf(node.type);
        j["actionOrConditionId"] = static_cast<int>(node.actionOrConditionId);
        WriteKeyAndParam(j, node);
        if (node.type == EditorBTNodeType::Decorator) j["decoratorType"] = node.decoratorType;
        j["children"] = nlohmann::json::array();

        node.runtimeFlatIndex = static_cast<int>(jsonNodes.size()); // デバッグ表示の対応付け
        jsonNodes.push_back(j);
        return node.runtimeFlatIndex;
    };

    // SubTree を「中で実際に動くノード」（入口の子。入れ子ならさらに中）へ解決する。
    //   ランタイムはサブツリーを知らないので、SubTree の場所へ中身をそのまま差し込む。
    //   経由した SubTree を via に積む。中身が無ければ nullptr。
    auto resolve = [&](EditorBTNode *n, std::vector<EditorBTNode *> &via) -> EditorBTNode * {
        for (size_t guard = 0; n && n->type == EditorBTNodeType::SubTree && guard <= m_nodes.size(); ++guard)
        {
            via.push_back(n);
            const EditorBTNode         *entry = FindSubTreeEntry(n->id);
            const std::vector<uint32_t> kids  = entry ? childrenOf(entry->id) : std::vector<uint32_t>{};
            if (kids.empty())
            {
                CCL_LOG_WARN(LogCategory::Editor, "[BT] サブツリー「%s」の入口に何も繋がっていません（常に失敗として扱います）",
                             n->name.c_str());
                return nullptr;
            }
            if (kids.size() > 1)
            {
                CCL_LOG_ERROR(LogCategory::Editor, "[BT] サブツリー「%s」の入口に複数の線が繋がっています。1本目だけを使います",
                              n->name.c_str());
            }
            n = FindNode(kids[0]);
        }
        return n;
    };

    // 子を1つ配列へ書き、その添字を返す。outBody には BFS で続きを辿るノード（無ければ nullptr）を返す
    auto bakeChild = [&](EditorBTNode &node, EditorBTNode *&outBody) -> int {
        std::vector<EditorBTNode *> via;
        outBody = resolve(&node, via);

        int index;
        if (outBody)
        {
            index = bakeOne(*outBody);
        }
        else
        {
            // 中身の無いサブツリーは空の Selector にする（子が無い Selector は常に Failure を返す）
            index = static_cast<int>(jsonNodes.size());
            jsonNodes.push_back({{"type", "Selector"}, {"actionOrConditionId", 0}, {"children", nlohmann::json::array()}});
        }

        if (!via.empty())
        {
            // 重みは「親から見た選ばれやすさ」なので、親から見えている外側の SubTree の値を使う
            auto &j = jsonNodes[index];
            if (via.front()->weight != 1.0f)
                j["weight"] = via.front()->weight;
            else
                j.erase("weight");

            // 外から見た SubTree も、中身と同じ実行状態で光るように同じ添字を振る
            for (EditorBTNode *v : via)
                v->runtimeFlatIndex = index;
        }
        return index;
    };

    // 幅優先（BFS）でフラット配列に並べる。親の children には子の添字を入れる。
    struct BakeTask
    {
        uint32_t nodeId;
        int      flatIndex;
    };
    std::queue<BakeTask> tasks;

    EditorBTNode *actualRoot = FindNode(rootChildren[0]);
    if (!actualRoot) return jsonNodes;
    {
        EditorBTNode *body  = nullptr;
        const int     index = bakeChild(*actualRoot, body);
        if (body) tasks.push({body->id, index});
    }

    while (!tasks.empty())
    {
        const BakeTask task = tasks.front();
        tasks.pop();

        for (uint32_t childId : childrenOf(task.nodeId))
        {
            EditorBTNode *child = FindNode(childId);
            if (!child) continue;

            EditorBTNode *body  = nullptr;
            const int     index = bakeChild(*child, body);
            jsonNodes[task.flatIndex]["children"].push_back(index);
            if (body) tasks.push({body->id, index});
        }
    }

    return jsonNodes;
}

// ====================================================================
// 親子関係と表示する階層
// ====================================================================
void BehaviorTreeWindow::RebuildHierarchy()
{
    m_childrenOf.clear();
    m_parentOf.clear();
    m_pinOwner.clear();
    m_hiddenNodes.clear();
    m_roots.clear();

    std::unordered_map<uint32_t, EditorBTNode *> byId;
    std::unordered_map<uint32_t, uint32_t>       outOwner; // 出力ピン → ノード
    std::unordered_map<uint32_t, uint32_t>       inOwner;  // 入力ピン → ノード
    for (auto &n : m_nodes)
    {
        byId[n.id] = &n;
        if (n.outputPin.id != 0)
        {
            outOwner[n.outputPin.id]   = n.id;
            m_pinOwner[n.outputPin.id] = n.id;
        }
        if (n.inputPin.id != 0)
        {
            inOwner[n.inputPin.id]    = n.id;
            m_pinOwner[n.inputPin.id] = n.id;
        }
    }

    for (const auto &link : m_links)
    {
        auto p = outOwner.find(link.startPinId);
        auto c = inOwner.find(link.endPinId);
        if (p == outOwner.end() || c == inOwner.end()) continue;

        m_childrenOf[p->second].push_back(c->second);
        m_parentOf.emplace(c->second, p->second); // 親が複数あれば最初の1本を採る
    }

    // 子を実行順に並べる。規則は「左が先。X が同じなら上が先」で、Bake・Priority 表示・
    // 自動整列はすべてこの並びを使う（規則をここ1か所に閉じ込めるため）。
    //   Y を第2キーにしているのは、縦に並べて X が揃ったときに順序が不定になるのを避けるため。
    //   Parallel は 0番目が主なので、不定だと「技」と「おまけ」が入れ替わる。
    for (auto &n : m_nodes)
        n.executionOrder = -1;
    for (auto &kv : m_childrenOf)
    {
        std::stable_sort(kv.second.begin(), kv.second.end(), [&](uint32_t a, uint32_t b) {
            const EditorBTNode *na = byId[a];
            const EditorBTNode *nb = byId[b];
            if (na->posX != nb->posX) return na->posX < nb->posX;
            return na->posY < nb->posY;
        });
        for (size_t i = 0; i < kv.second.size(); ++i)
            byId[kv.second[i]]->executionOrder = static_cast<int>(i) + 1;
    }

    // 根＝親を持たないノード。ROOT 型を先頭に置く（整列でも一番左に来る）。
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group) continue;
        if (m_parentOf.count(n.id) == 0) m_roots.push_back(n.id);
    }
    std::stable_sort(m_roots.begin(), m_roots.end(), [&](uint32_t a, uint32_t b) {
        const bool ra = byId[a]->type == EditorBTNodeType::Root;
        const bool rb = byId[b]->type == EditorBTNodeType::Root;
        return ra && !rb;
    });

    // 根から辿れるノードに印を付ける（辿れない塊を下で拾うため）
    std::unordered_set<uint32_t>  visited;
    std::function<void(uint32_t)> walk = [&](uint32_t id) {
        if (!visited.insert(id).second) return; // 循環や、親が2つある子で止める

        auto it = m_childrenOf.find(id);
        if (it == m_childrenOf.end()) return;
        for (uint32_t c : it->second)
            walk(c);
    };
    for (uint32_t r : m_roots)
        walk(r);

    // どの根からも辿れなかった塊（リンクが循環している等）も根として扱う（整列で置き去りにしない）。
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group || visited.count(n.id)) continue;
        m_roots.push_back(n.id);
        walk(n.id);
    }

    // 今見ている階層に無いノード（サブツリーの中身・潜っているときの外側）は描かない。
    //   「隠れているノード」として1か所に集めておけば、描画・選択・整列が同じ扱いで済む。
    const uint32_t view = CurrentGraphId();
    for (const auto &n : m_nodes)
    {
        if (n.graphId != view) m_hiddenNodes.insert(n.id);
    }
}

bool BehaviorTreeWindow::HasChildren(uint32_t nodeId) const
{
    auto it = m_childrenOf.find(nodeId);
    return it != m_childrenOf.end() && !it->second.empty();
}

int BehaviorTreeWindow::CountDescendants(uint32_t nodeId) const
{
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t>        stack = {nodeId};
    while (!stack.empty())
    {
        const uint32_t id = stack.back();
        stack.pop_back();

        auto it = m_childrenOf.find(id);
        if (it == m_childrenOf.end()) continue;
        for (uint32_t c : it->second)
        {
            if (seen.insert(c).second) stack.push_back(c);
        }
    }
    return static_cast<int>(seen.size());
}
