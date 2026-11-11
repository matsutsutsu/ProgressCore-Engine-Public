#include "Editor/Windows/AI/BehaviorTreeWindow.h"
#include "Editor/Windows/AI/BehaviorTreeEditorCommon.h"
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"
#include "Engine/Platform/Core/Logger.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <queue>

using namespace BTEditor;

// ---------------------------------------------------------------------------
// グラフ ⇔ JSON
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::WriteKeyAndParam(nlohmann::json &j, const EditorBTNode &node)
{
    // 行動/条件は数値IDでなく名前（保存キー）で書く。IDは追加のたびに増えるだけの内部都合で、
    // JSON を開いた人には読めない。Selector などに "action": "None" が並ばないよう、意味のある種類にだけ書く
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

    // 割り込みの印も、付けたときだけ書く。既存の木にキーが増えないようにするため
    if (node.interrupt)
        j["interrupt"] = true;
    if (node.abortSelf)
        j["abortSelf"] = true;

    // 採点ルールも、使うときだけ書く。読み手は BehaviorTreeLoader::ReadScoreRule と NodeFromJson（キーを揃えること）
    if (node.useScoreRule)
    {
        const BTScoreRule &r = node.scoreRule;
        j["score"] = {
            {"base", r.base},
            {"distance", {{"bestMin", r.bestMin}, {"bestMax", r.bestMax}, {"falloff", r.falloff}}},
        };

        // 状況による倍率の条件は保存キー（名前）で書く。行動・条件ノードと同じ理由（数値IDは辞書の並びで変わる）
        nlohmann::json bonus = nlohmann::json::array();
        for (int i = 0; i < r.bonusCount; ++i)
        {
            const BTScoreBonus &b = r.bonuses[i];
            const ActionEntry  *e = FindConditionEntry(b.conditionId);
            bonus.push_back({{"condition", e ? e->key : "None"}, {"param", b.param}, {"mult", b.mult}});
        }
        if (!bonus.empty())
            j["score"]["bonus"] = bonus;

        // クールダウンも付けたときだけ書く
        if (r.cooldown > 0.0f)
            j["score"]["cooldown"] = r.cooldown;
    }
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

    // ピンのID。リンクはピンIDで繋がっているので、これがないとリンクを復元できない
    jn["inputPinId"]  = node.inputPin.id;
    jn["outputPinId"] = node.outputPin.id;

    // 描画前のノード（生成・整列・Undo の直後）は、ライブラリ側の座標がまだ古い
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
    node.param     = jn.value("param", 0.0f);
    node.weight    = jn.value("weight", 1.0f);
    node.interrupt = jn.value("interrupt", false);
    node.abortSelf = jn.value("abortSelf", false);

    // 採点ルール。キーがなければ使わない（weight が点数）
    node.useScoreRule = jn.contains("score") && jn["score"].is_object();
    if (node.useScoreRule)
    {
        const nlohmann::json &js = jn["score"];
        BTScoreRule          &r  = node.scoreRule;
        r.base = js.value("base", r.base);
        if (js.contains("distance") && js["distance"].is_object())
        {
            const nlohmann::json &jd = js["distance"];
            r.bestMin = jd.value("bestMin", r.bestMin);
            r.bestMax = jd.value("bestMax", r.bestMax);
            r.falloff = jd.value("falloff", r.falloff);
        }

        // 状況による倍率。未知の条件も ID 0（None）のまま残す。エディタで消さずに見せて、選び直せるようにするため
        if (js.contains("bonus") && js["bonus"].is_array())
        {
            for (const auto &jb : js["bonus"])
            {
                if (r.bonusCount >= kMaxScoreBonuses) break;

                BTScoreBonus &b = r.bonuses[r.bonusCount++];
                ActionID      id = 0;
                TryFindConditionIdByKey(jb.value("condition", std::string()), id);
                b.conditionId = id;
                b.param       = jb.value("param", 0.0f);
                b.mult        = jb.value("mult", 1.0f);
            }
        }

        r.cooldown = js.value("cooldown", 0.0f);
    }

    node.posX    = jn.value("posX", 0.0f);
    node.posY    = jn.value("posY", 0.0f);

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
    // 実行中の状態表示（デバッグ）が正しいノードに出る
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



// ---------------------------------------------------------------------------
// ファイル
// ---------------------------------------------------------------------------

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
    // 処理フロー:
    // 1. ファイルを読んで JSON にする
    // 2. グラフを置き換える
    // 3. Undo の基準と未保存の印を引き直す

    // ---------------------------------------------------------
    // 1. ファイルと JSON
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 2. グラフの置き換え
    // ---------------------------------------------------------
    ed::SetCurrentEditor(m_nodeContext);
    ed::ClearSelection(); // 前のグラフのノードIDが選択に残らないように
    ApplyGraphJson(j);

    m_needInitialZoom = true;

    // ---------------------------------------------------------
    // 3. Undo の基準
    // ---------------------------------------------------------

    // Undo の基準をこのグラフに引き直す
    //   忘れると m_lastSavedState は「前に開いていたBT」のままで、読込後の最初の編集の
    //   『戻し先』が別のBTになる。Ctrl+Z → 保存で、今のファイルが前のBTで上書きされる
    m_lastSavedState = GetGraphStateAsJson();
    m_dirty          = false;
    m_currentPath    = path;

    CCL_LOG_SUCCESS(LogCategory::Editor, "AIグラフをロードしました: %s", path.c_str());
    return true;
}



// ---------------------------------------------------------------------------
// テキスト書き出し
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::ExportOutlineText(const std::filesystem::path &path)
{
    // path をそのまま ofstream に渡す。Windows では wchar_t のパスとして開くので、日本語のフォルダでも開ける
    std::ofstream file(path);
    if (!file)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "BTのテキストを書き出せませんでした: %s", FileDialog::ConvertToRelativePath(path).c_str());
        return;
    }

    const std::string text = BuildOutlineText();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    CCL_LOG_SUCCESS(LogCategory::Editor, "BTをテキストで書き出しました: %s", FileDialog::ConvertToRelativePath(path).c_str());
}



std::string BehaviorTreeWindow::BuildOutlineText()
{
    // 処理フロー:
    // 1. 凡例（AI が木を読むための前提）
    // 2. ROOT から辿った木
    // 3. ROOT に繋がっていないノード

    // 出力の形:
    // ROOT
    // [Selector] Root Selector
    // [Sequence] 縦割り
    // [Decorator] Cooldown 7秒
    // [Sequence] ...
    // 辿り方はアウトラインと同じ（根から m_childrenOf を再帰で辿る）

    // ボタンはキャンバスより先に処理されるので、念のためこの時点の親子関係を作り直してから辿る
    RebuildHierarchy();

    std::string out;

    // ---------------------------------------------------------
    // 1. 凡例
    // ---------------------------------------------------------

    // BehaviorTreeSystem.cpp の実際の評価の仕方に合わせてある。ランタイムの挙動を変えたら凡例も直すこと
    // AI はこの説明を前提に助言するので、ずれると誤った助言になる
    out += "# ビヘイビアツリー: " + (m_currentPath.empty() ? std::string("(未保存)") : FileNameOf(m_currentPath)) + "\n";
    out += R"(#
# 読み方
#   - 字下げが親子関係。兄弟は上にあるものから順に評価される（上ほど優先）
#   - 毎フレーム ROOT から評価する。ただし Selector / Sequence は「実行中(Running)の子」を覚えていて、
#     次のフレームはその子から再開する。実行中の子より上の兄弟は、その子が終わるまで評価し直さない
#
# ノードの種類
#   [Selector]         子を上から試し、成功か実行中の子が出たらそこで止まる（OR）。全部失敗なら失敗
#   [Sequence]         子を上から順に実行し、1つ失敗したらそこで失敗（AND）。全部成功なら成功
#   [WeightedSelector] 子を weight に比例した確率で1つ引いて実行する。失敗したら候補から外して引き直す
#   [UtilitySelector]  先頭の条件が通らない子・クールダウン中の子を外し、残りを点数の高い順に並べ、
#                      上位3つから点数に比例して1つ引く。失敗したら外して詰め、次点を繰り上げて引き直す
#                        点数 : score が付いた子は 優先度 × 距離の倍率。得意な距離 a〜b m では満点で、
#                               範囲の外へ出るほど下がり、ぼかし幅 c m 離れると0点（候補外）。
#                               続く「条件(値) xN」は、その条件が通ると点数を N 倍する（状況による倍率）。
#                               score が無い子は weight が点数。
#                               どの子も、選ばれてから10秒は点数が下がる（直後は0.3倍、10秒で元に戻る）
#   [Parallel]         1番目の子（主）の結果を返す。主が実行中の間だけ、2番目以降（従）も毎フレーム動かす
#   [Decorator]        1つだけの子の結果を修飾する
#                        Cooldown N秒 : 子が成功したら、N 秒間この枝ごと失敗を返す
#                        Retry N回    : 子が失敗したら、同じフレーム内で N 回まで試し直す
#                        Inverter     : 成功と失敗を入れ替える
#   [Condition]        条件を評価して成功/失敗を返す（その場で終わる）
#   [Action]           行動を実行する。時間のかかる行動は、終わるまで実行中(Running)を返す

)";

    // ---------------------------------------------------------
    // 2. 木
    // ---------------------------------------------------------

    // ROOT に繋がっていない塊は 3 へ回す
    std::unordered_set<uint32_t> visited;
    std::vector<uint32_t>        unconnected;
    for (uint32_t root : m_roots)
    {
        const EditorBTNode *n = FindNode(root);
        if (!n) continue;

        if (n->type == EditorBTNodeType::Root)
            AppendOutlineLines(out, root, 0, 0, visited);
        else
            unconnected.push_back(root);
    }

    // ---------------------------------------------------------
    // 3. 未接続
    // ---------------------------------------------------------

    // 作りかけのノードも見せておく（「これはなぜ動かないのか」を相談できるように）
    if (!unconnected.empty())
    {
        out += "\n# 以下は ROOT に繋がっていないので、実行されない\n";
        for (uint32_t id : unconnected)
            AppendOutlineLines(out, id, 0, 0, visited);
    }
    return out;
}



void BehaviorTreeWindow::AppendOutlineLines(std::string &out, uint32_t nodeId, uint32_t parentId, int depth,
                                            std::unordered_set<uint32_t> &visited)
{
    // 同じノードを2回書かない（リンクが輪になっていても止まる）。アウトラインと同じ理由
    if (!visited.insert(nodeId).second) return;

    // 処理フロー:
    // 1. この行：「字下げ [種類] 表示名  (注記)」
    // 2. 子の行（再帰）

    const EditorBTNode *node = FindNode(nodeId);
    if (!node) return;

    // ---------------------------------------------------------
    // 1. この行
    // ---------------------------------------------------------
    std::string line(static_cast<size_t>(depth) * 2, ' '); // 1段につき空白2つ

    const char *typeName = "";
    switch (node->type)
    {
    case EditorBTNodeType::Selector: typeName = "Selector"; break;
    case EditorBTNodeType::Sequence: typeName = "Sequence"; break;
    case EditorBTNodeType::WeightedSelector: typeName = "WeightedSelector"; break;
    case EditorBTNodeType::Parallel: typeName = "Parallel"; break;
    case EditorBTNodeType::UtilitySelector: typeName = "UtilitySelector"; break;
    case EditorBTNodeType::Decorator: typeName = "Decorator"; break;
    case EditorBTNodeType::Condition: typeName = "Condition"; break;
    case EditorBTNodeType::Action: typeName = "Action"; break;
    default: break;
    }

    if (node->type == EditorBTNodeType::Root)
        line += "ROOT";
    else
        line += std::string("[") + typeName + "] " + GetDisplayName(*node);

    // 親から見た注記。重みと主従は、その子自身ではなく「親の種類」で意味が決まる値なので、親を見て付ける
    if (const EditorBTNode *parent = FindNode(parentId))
    {
        if (parent->type == EditorBTNodeType::UtilitySelector && node->useScoreRule)
        {
            const BTScoreRule &r = node->scoreRule;
            char               score[96];
            snprintf(score, sizeof(score), "  (score 優先度 %g, 得意な距離 %g〜%g m, ぼかし %g m",
                     r.base, r.bestMin, r.bestMax, r.falloff);
            line += score;

            // 状況による倍率は「条件(値) x倍率」を並べる
            for (int i = 0; i < r.bonusCount; ++i)
            {
                const BTScoreBonus &b = r.bonuses[i];
                const ActionEntry  *e = FindConditionEntry(b.conditionId);
                snprintf(score, sizeof(score), ", %s(%g) x%g", e ? e->key : "None", b.param, b.mult);
                line += score;
            }
            if (r.cooldown > 0.0f)
            {
                snprintf(score, sizeof(score), ", CD %g 秒（選んだ瞬間から）", r.cooldown);
                line += score;
            }
            line += ")";
        }
        else if (parent->type == EditorBTNodeType::WeightedSelector || parent->type == EditorBTNodeType::UtilitySelector)
        {
            char weight[32];
            snprintf(weight, sizeof(weight), "%g", node->weight);
            line += std::string("  (weight ") + weight + ")";
        }
        if (parent->type == EditorBTNodeType::Parallel)
        {
            const bool isMain = m_childrenOf[parentId].front() == nodeId; // 1番目の子が主
            line += isMain ? "  (主)" : "  (従)";
        }
    }

    out += line + "\n";

    // ---------------------------------------------------------
    // 2. 子の行
    // ---------------------------------------------------------
    auto children = m_childrenOf.find(nodeId);
    if (children == m_childrenOf.end()) return;
    for (uint32_t child : children->second)
        AppendOutlineLines(out, child, nodeId, depth + 1, visited);
}



// ---------------------------------------------------------------------------
// Bake
// ---------------------------------------------------------------------------

nlohmann::json BehaviorTreeWindow::BuildBakedNodes()
{
    // 処理フロー:
    // 1. 親子関係を作り直し、前回の添字を消す
    // 2. ROOT の直下を探す
    // 3. 幅優先でフラット配列に並べる

    // ランタイムはこの配列だけを読むので、エディタ側の構造（ピン・リンク・コメント枠）は自由に変えてよい

    nlohmann::json jsonNodes = nlohmann::json::array();

    // ---------------------------------------------------------
    // 1. 準備
    // ---------------------------------------------------------

    // 子の並び（＝実行順）は RebuildHierarchy が決める。読み込み直後など、
    // まだ今フレームの再構築を通っていない時にも呼ばれるので、ここで作り直す
    RebuildHierarchy();

    // 木から外れたノードに古い添字が残ると、デバッグ表示で別ノードの状態が出る
    for (auto &n : m_nodes)
        n.runtimeFlatIndex = -1;

    // ---------------------------------------------------------
    // 2. ROOT の直下
    // ---------------------------------------------------------
    auto rootIt = std::find_if(m_nodes.begin(), m_nodes.end(),
                               [](const EditorBTNode &n) { return n.type == EditorBTNodeType::Root; });
    if (rootIt == m_nodes.end()) return jsonNodes;

    auto childrenOf = [&](uint32_t id) -> std::vector<uint32_t> {
        auto it = m_childrenOf.find(id);
        return (it == m_childrenOf.end()) ? std::vector<uint32_t>{} : it->second;
    };

    const std::vector<uint32_t> rootChildren = childrenOf(rootIt->id);
    if (rootChildren.empty()) return jsonNodes;

    if (rootChildren.size() > 1)
    {
        CCL_LOG_ERROR(LogCategory::Editor, "[BT] ROOT から出せる線は1本だけ。Selector などを1つ挟んで分岐させる");
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
        case EditorBTNodeType::UtilitySelector: return "UtilitySelector";
        default: return "Unknown";
        }
    };

    // ランタイム用の1ノード分。ROOT 直下も子も必ずここを通す
    //   ROOT 直下だけ別に書くと、書き漏れた項目（decoratorType 等）が読み込み側の既定値に
    //   なる（Cooldown が既定値 0 の Inverter として動く）
    auto bakeOne = [&](EditorBTNode &node) {
        nlohmann::json j;
        j["type"]                = runtimeTypeOf(node.type);
        j["actionOrConditionId"] = static_cast<int>(node.actionOrConditionId);
        WriteKeyAndParam(j, node);
        if (node.type == EditorBTNodeType::Decorator) j["decoratorType"] = node.decoratorType;
        // 名前はゲーム中のデバッグ表示（技選択のパネル）に出す。評価には使わない
        if (UsesCustomName(node.type) && !node.name.empty()) j["name"] = node.name;
        if (node.type == EditorBTNodeType::Action && node.actionOrConditionId == A_DroneTechnique)
            j["name"] = GetDisplayName(node); // 技選択の直下に1つで置くので、パネルに技の名前を出す
        j["children"] = nlohmann::json::array();

        node.runtimeFlatIndex = static_cast<int>(jsonNodes.size()); // デバッグ表示の対応付け
        jsonNodes.push_back(j);
        return node.runtimeFlatIndex;
    };

    // ---------------------------------------------------------
    // 3. 幅優先で並べる
    // ---------------------------------------------------------

    // 親の children には子の添字を入れる
    struct BakeTask
    {
        uint32_t nodeId;
        int      flatIndex;
    };
    std::queue<BakeTask> tasks;

    EditorBTNode *actualRoot = FindNode(rootChildren[0]);
    if (!actualRoot) return jsonNodes;
    tasks.push({actualRoot->id, bakeOne(*actualRoot)});

    while (!tasks.empty())
    {
        const BakeTask task = tasks.front();
        tasks.pop();

        for (uint32_t childId : childrenOf(task.nodeId))
        {
            EditorBTNode *child = FindNode(childId);
            if (!child) continue;

            const int index = bakeOne(*child);
            jsonNodes[task.flatIndex]["children"].push_back(index);
            tasks.push({child->id, index});
        }
    }

    return jsonNodes;
}



// ---------------------------------------------------------------------------
// 親子関係
// ---------------------------------------------------------------------------

void BehaviorTreeWindow::RebuildHierarchy()
{
    // 処理フロー:
    // 1. ピンの持ち主の表を作る
    // 2. リンクから親子をつなぐ
    // 3. 子を実行順に並べる
    // 4. 根の一覧を作る

    // 変更経路（リンク編集・Undo・読込・ドラッグでの順序入れ替え）が多いので、汚れフラグで追うより毎回作る
    // 数百ノードまでなら無視できるコスト
    ZoneScopedN("BTW: RebuildHierarchy");
    ZoneValue(m_nodes.size());

    m_childrenOf.clear();
    m_parentOf.clear();
    m_pinOwner.clear();
    m_roots.clear();

    // ---------------------------------------------------------
    // 1. ピンの持ち主
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 2. 親子
    // ---------------------------------------------------------
    for (const auto &link : m_links)
    {
        auto p = outOwner.find(link.startPinId);
        auto c = inOwner.find(link.endPinId);
        if (p == outOwner.end() || c == inOwner.end()) continue;

        m_childrenOf[p->second].push_back(c->second);
        m_parentOf.emplace(c->second, p->second); // 親が複数あれば最初の1本を採る
    }

    // ---------------------------------------------------------
    // 3. 実行順
    // ---------------------------------------------------------

    // 規則は「左が先。X が同じなら上が先」で、Bake・Priority 表示・
    // 自動整列はすべてこの並びを使う（規則をここ1か所に閉じ込めるため）

    // Y を第2キーにしているのは、縦に並べて X が揃ったときに順序が不定になるのを避けるため
    // Parallel は 0番目が主なので、不定だと「技」と「おまけ」が入れ替わる
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

    // ---------------------------------------------------------
    // 4. 根
    // ---------------------------------------------------------

    // 根＝親を持たないノード。ROOT 型を先頭に置く（整列でも一番左に来る）
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

    // どの根からも辿れなかった塊（リンクが循環している等）も根として扱う（整列で置き去りにしない）
    for (const auto &n : m_nodes)
    {
        if (n.type == EditorBTNodeType::Group || visited.count(n.id)) continue;
        m_roots.push_back(n.id);
        walk(n.id);
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
