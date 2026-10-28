#include "BehaviorTreeLoader.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <json.hpp>
#include "Game/Logics/AI/BehaviorTree/Data/BTActionIds.h"

#include <cstdint>
#include <utility>
#include <vector>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// 読み込み
// ---------------------------------------------------------------------------

bool BehaviorTreeLoader::LoadFromJson(const std::string &path, BTAsset &outAsset)
{
    // 処理フロー:
    // 1. ファイルを読んで JSON にする
    // 2. ノードを1つずつ BTNode にする
    // 3. 木の形を検査する（壊れていれば読まない）
    // 4. 意図どおりに動かない形を警告する

    // ---------------------------------------------------------
    // 1. ファイルと JSON
    // ---------------------------------------------------------
    const std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty())
    {
        CCL_LOG_ERROR(LogCategory::AI,
                      "Failed to load BT file: %s (FileSystem returned empty)", path.c_str());
        return false;
    }

    nlohmann::json j;
    try
    {
        j = json::parse(jsonStr);
    }
    catch (const std::exception &e)
    {
        CCL_LOG_ERROR(LogCategory::AI, "JSON Parse Error in %s: %s", path.c_str(), e.what());
        return false;
    }

    // ---------------------------------------------------------
    // 2. ノード
    // ---------------------------------------------------------
    outAsset.id = j.value("assetId", 0u);
    outAsset.nodes.clear();
    outAsset.scoreRules.clear();
    outAsset.nodeNames.clear();

    if (j.contains("nodes") && j["nodes"].is_array())
    {
        for (auto &jNode : j["nodes"])
        {
            BTNode node;
            node.type                = StringToNodeType(jNode.value("type", "Action"));
            node.actionOrConditionId = ResolveActionOrConditionId(&jNode, node.type, path);

            node.decoratorType = static_cast<BTDecoratorType>(jNode.value("decoratorType", 0));

            node.param = jNode.value("param", 0.0f);

            // 抽選の重み。書かれていなければ 1.0＝均等。
            //   エディタは 1.0 のとき "weight" を書き出さないので、重みを一度も
            //   触っていない木にはキー自体が無い。既定値はその穴を埋めるためにある。
            node.weight = jNode.value("weight", 1.0f);

            // 割り込みの印。付いていない枝は、上の枝が終わるまで待つ。
            node.interrupt = jNode.value("interrupt", false);

            // 自己中断の印。付いていない枝は、条件が崩れても最後まで走り切る。
            node.abortSelf = jNode.value("abortSelf", false);

            // 採点ルール（UtilitySelector の子だけが使う）。書かれていなければ weight が点数になる。
            //   ルールはノードに直接持たせず、木の scoreRules に並べてノードは添字だけを持つ。
            //   ルールを使うのは一部の子だけなので、全ノードに数十バイトずつ足すと
            //   「添字の近いノードが同じキャッシュラインに載る」利点（BehaviorTreeData.h の備考）が薄れるため。
            if (jNode.contains("score") && jNode["score"].is_object())
            {
                node.scoreRuleIndex = static_cast<int16_t>(outAsset.scoreRules.size());
                outAsset.scoreRules.push_back(ReadScoreRule(&jNode["score"], path));
            }

            if (jNode.contains("children") && jNode["children"].is_array())
            {
                for (auto &childId : jNode["children"])
                {
                    node.children.push_back(childId.get<BTNodeID>());
                }
            }

            outAsset.nodes.push_back(node);

            // 名前（デバッグ表示用）。エディタは名前を付けたまとまりにだけ書くので、無ければ空
            outAsset.nodeNames.push_back(jNode.value("name", std::string()));
        }
    }

    if (outAsset.nodes.empty())
    {
        CCL_LOG_ERROR(LogCategory::AI,
                      "BT file has no runtime nodes[]: %s (editor_nodes のみの可能性)", path.c_str());
        return false;
    }

    // ---------------------------------------------------------
    // 3. 木の形
    // ---------------------------------------------------------

    // 構造が壊れている木は、この先の ValidateNodes が範囲外を読む。必ず先に通す。
    if (!ValidateStructure(outAsset, path))
    {
        outAsset.nodes.clear();
        return false;
    }

    // ---------------------------------------------------------
    // 4. 意図どおりに動かない形
    // ---------------------------------------------------------
    ValidateNodes(outAsset, path);

    CCL_LOG_SUCCESS(LogCategory::AI, "BT Asset Loaded: %s (Nodes: %zu)", path.c_str(), outAsset.nodes.size());
    return true;
}

ActionID BehaviorTreeLoader::ResolveActionOrConditionId(const void *jNodePtr, BTNodeType type,
                                                        const std::string &path)
{
    // 行動・条件は保存キー（"A_BossLeapAttack" などの文字列）で解決する。
    // 数値IDだと、辞書の並びを変えた瞬間に保存済みのアセットが別の技を指すため。

    // アセットには数値IDも併記されていて、キーを引けなかったときだけそちらを使う。
    // 知らないキーを 0（None）にしないのは、打ち間違えただけで木が丸ごと動かなくなるより、動いたまま警告が出る方が気づけるため。
    const json &jNode = *static_cast<const json *>(jNodePtr);

    const ActionID numericId = static_cast<ActionID>(jNode.value("actionOrConditionId", 0));

    const bool  isCondition = (type == BTNodeType::Condition);
    const char *keyField    = isCondition ? "condition" : "action";
    if (!jNode.contains(keyField) || !jNode[keyField].is_string()) return numericId;

    const std::string key = jNode[keyField].get<std::string>();
    if (key.empty() || key == "None") return numericId;

    ActionID   resolved = 0;
    const bool found    = isCondition ? TryFindConditionIdByKey(key, resolved)
                                      : TryFindActionIdByKey(key, resolved);
    if (found) return resolved;

    CCL_LOG_WARN(LogCategory::AI,
                 "BTの未知のキー \"%s\" (%s)。数値ID %u で読み込む", key.c_str(), path.c_str(), numericId);
    return numericId;
}

BTScoreRule BehaviorTreeLoader::ReadScoreRule(const void *jScorePtr, const std::string &path)
{
    // 形: "score": { "base": 10,
    //                "distance": { "bestMin": 3, "bestMax": 8, "falloff": 2 },
    //                "bonus": [ { "condition": "C_RecentlyHit", "param": 0.6, "mult": 3.0 } ] }
    // 書き手はエディタ（BehaviorTreeWindow::WriteKeyAndParam）。キーの名前を変えるときは両方を直すこと。
    const json &jScore = *static_cast<const json *>(jScorePtr);

    BTScoreRule rule; // 書かれていない項目は既定値のまま
    rule.base = jScore.value("base", rule.base);

    if (jScore.contains("distance") && jScore["distance"].is_object())
    {
        const json &jDist = jScore["distance"];
        rule.bestMin      = jDist.value("bestMin", rule.bestMin);
        rule.bestMax      = jDist.value("bestMax", rule.bestMax);
        rule.falloff      = jDist.value("falloff", rule.falloff);
    }

    if (jScore.contains("bonus") && jScore["bonus"].is_array())
    {
        for (const json &jBonus : jScore["bonus"])
        {
            // 固定長なので、上限を超えた分は読まない（黙って捨てると「付けたのに効かない」になるので言う）
            if (rule.bonusCount >= kMaxScoreBonuses)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] 採点ルールの状況による倍率は %d 個まで。超えた分は読まない: %s",
                             kMaxScoreBonuses, path.c_str());
                break;
            }

            // 条件は保存キー（"C_RecentlyHit" など）で引く。条件ノードと同じ理由（数値IDは辞書の並びで変わる）
            const std::string key = jBonus.value("condition", std::string());
            ActionID          id  = 0;
            if (!TryFindConditionIdByKey(key, id))
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] 採点ルールの状況による倍率に未知の条件 \"%s\"。この倍率は読まない: %s",
                             key.c_str(), path.c_str());
                continue;
            }

            BTScoreBonus &bonus = rule.bonuses[rule.bonusCount++];
            bonus.conditionId   = id;
            bonus.param         = jBonus.value("param", 0.0f);
            bonus.mult          = jBonus.value("mult", 1.0f);
        }
    }
    return rule;
}

// ---------------------------------------------------------------------------
// 検査
// ---------------------------------------------------------------------------

namespace
{
    // 枝の中に Running を返しうる行動が1つでもあるか（無ければ、その枝は毎フレームすぐ終わる）
    bool SubtreeCanBlock(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action && IsBlockingAction(node.actionOrConditionId))
            return true;
        for (BTNodeID c : node.children)
            if (SubtreeCanBlock(asset, c)) return true;
        return false;
    }

    // 枝の中に currentActionId を書く行動が1つでもあるか
    bool SubtreeWritesCommand(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action && IsCommandWritingAction(node.actionOrConditionId))
            return true;
        for (BTNodeID c : node.children)
            if (SubtreeWritesCommand(asset, c)) return true;
        return false;
    }

    // 枝の中に条件があるか（失敗して次の枝へ進む可能性があるか）
    bool SubtreeHasCondition(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Condition) return true;
        for (BTNodeID c : node.children)
            if (SubtreeHasCondition(asset, c)) return true;
        return false;
    }
} // namespace

bool BehaviorTreeLoader::ValidateStructure(const BTAsset &asset, const std::string &path)
{
    // 処理フロー:
    // 1. 子の添字が範囲内か
    // 2. 循環していないか
    // 3. root から辿れないノードが無いか（警告だけ）

    // 【備考】
    //   1 と 2 は「警告して続行」ではなく「読まない」にする。ValidateNodes も評価も asset.nodes[...] を
    //   範囲チェックせずに再帰でたどるので、壊れた木はログではなく異常終了になるため。

    const size_t n = asset.nodes.size();

    // ---------------------------------------------------------
    // 1. 子の添字の範囲
    // ---------------------------------------------------------

    // 1つでも範囲外があると、この直後の ValidateNodes が asset.nodes[child] を
    // そのまま読む。std::vector::operator[] なので検出もされない。
    bool ranged = true;
    for (size_t i = 0; i < n; ++i)
    {
        for (const BTNodeID c : asset.nodes[i].children)
        {
            if (static_cast<size_t>(c) < n) continue;
            CCL_LOG_ERROR(LogCategory::AI,
                          "[BT] nodes[%zu] の子 %u がノード数 %zu の範囲外。木が壊れている "
                          "（BTエディタで開き直して保存すると直ることが多い）: %s",
                          i, static_cast<unsigned>(c), n, path.c_str());
            ranged = false;
        }
    }
    if (!ranged) return false;

    // ---------------------------------------------------------
    // 2. 循環
    // ---------------------------------------------------------

    // 評価(EvaluateTreeRecursive)も検証(SubtreeCanBlock)も再帰で降りるので、
    // 1周でもあるとスタックが尽きて落ちる。

    // ここ自体は再帰を使わない。壊れた入力から守る側が同じ理由で落ちないようにするため。
    enum : uint8_t
    {
        kWhite = 0,
        kGray  = 1,
        kBlack = 2
    };
    std::vector<uint8_t>                   color(n, kWhite);
    std::vector<std::pair<size_t, size_t>> stack; // (ノード, 次に見る子の添字)

    for (size_t root = 0; root < n; ++root)
    {
        if (color[root] != kWhite) continue;

        color[root] = kGray;
        stack.push_back({root, 0});

        while (!stack.empty())
        {
            const size_t node = stack.back().first;
            const size_t idx  = stack.back().second;
            const auto  &kids = asset.nodes[node].children;

            if (idx >= kids.size())
            {
                color[node] = kBlack;
                stack.pop_back();
                continue;
            }
            stack.back().second = idx + 1; // push_back で参照が無効になる前に進める

            const BTNodeID c = kids[idx];
            if (color[c] == kGray)
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[BT] nodes[%zu] から nodes[%u] への辺が循環になっている。"
                              "評価に入るとスタックが尽きる: %s",
                              node, static_cast<unsigned>(c), path.c_str());
                return false;
            }
            if (color[c] == kWhite)
            {
                color[c] = kGray;
                stack.push_back({static_cast<size_t>(c), 0});
            }
        }
    }

    // ---------------------------------------------------------
    // 3. root(0) から辿り着けないノード
    // ---------------------------------------------------------

    // 落ちはしないが、置いたのに繋ぎ忘れた枝がここで分かる。

    // ERROR にはしない。デバッグの「強制実行」は任意のノードから始められるため。
    std::vector<bool>   seen(n, false);
    std::vector<size_t> work{0};
    while (!work.empty())
    {
        const size_t u = work.back();
        work.pop_back();
        if (seen[u]) continue;
        seen[u] = true;
        for (const BTNodeID c : asset.nodes[u].children)
            work.push_back(static_cast<size_t>(c));
    }
    size_t unreachable = 0;
    for (size_t i = 0; i < n; ++i)
        if (!seen[i]) ++unreachable;
    if (unreachable > 0)
    {
        CCL_LOG_WARN(LogCategory::AI,
                     "[BT] root(nodes[0]) から辿れないノードが %zu 個ある。"
                     "繋ぎ忘れた枝は一度も評価されない: %s",
                     unreachable, path.c_str());
    }

    return true;
}

void BehaviorTreeLoader::ValidateNodes(const BTAsset &asset, const std::string &path)
{
    // 処理フロー（ノード1つごとに）:
    // 1. デコレーターの設定値
    // 2. 並行ノードの主と従
    // 3. 選択系の下の「毎フレーム即 Success」の枝
    // 4. 割り込みの印の置き場所
    // 5. 自己中断の印の置き場所
    // 6. 陣形指示の撃ち忘れ
    // 7. 抽選の重み
    // 8. 抽選できる子の数
    // 9. 採点ルールの距離の並び

    // どの検査も「木としては読めるが、置いた本人の意図どおりには動かない」形を見つけるためのもの。
    // 評価はワーカースレッドで並列に走りファイル名も持たないので、ファイル名付きで言えるのは読み込み時だけ。
    for (size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const BTNode &node = asset.nodes[i];

        // ---------------------------------------------------------
        // 1. デコレーター
        // ---------------------------------------------------------
        if (node.type == BTNodeType::Decorator)
        {
            // クールダウン0秒＝毎フレーム通過。新規作成の param は 0 なので、
            // 「Cooldown を選んだが待機時間を触っていない」だけでこうなる。
            if (node.decoratorType == BTDecoratorType::Cooldown && node.param <= 0.0f)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] のクールダウンが %.2f 秒。0以下は毎フレーム素通りする"
                             "（＝クールダウンが効かない）: %s",
                             i, node.param, path.c_str());
            }
            // 試行回数0＝子を一度も評価せず、常に Failure になる。
            if (node.decoratorType == BTDecoratorType::Retry && node.param < 1.0f)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] のリトライ回数が %.0f。1未満は子を評価できない: %s",
                             i, node.param, path.c_str());
            }
            if (node.children.empty())
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] のデコレーターに子が繋がっていない: %s", i, path.c_str());
            }
            // 存在しない種別。評価の switch はどの case にも当たらず素通りするので、
            // 修飾を付けたつもりのノードが何もしない。
            if (static_cast<int>(node.decoratorType) > static_cast<int>(BTDecoratorType::Retry))
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の修飾の種類が %d。この番号は存在しないので"
                             "何も修飾せず素通りする: %s",
                             i, static_cast<int>(node.decoratorType), path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 2. 並行ノード
        // ---------------------------------------------------------

        // 主(children[0])と従(それ以降)で意味が違うので、置き間違えると
        // 「従が一度も動かない」「技が終わっても体が止まらない」になる。どちらも実行中は静かなので、ここで言う。
        if (node.type == BTNodeType::Parallel)
        {
            if (node.children.empty())
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の並行ノードに子が繋がっていない: %s", i, path.c_str());
            }
            else
            {
                // 主が Running を返さないなら、従は一度も評価されない。
                // 陣形指示のように即 Success を返す行動を主に置くと起きる。
                if (!SubtreeCanBlock(asset, node.children[0]) && node.children.size() > 1)
                {
                    CCL_LOG_ERROR(LogCategory::AI,
                                  "[BT] nodes[%zu] の並行ノードの主(nodes[%u])は Running を返さない"
                                  "（待機も時間のかかる技も無い）。従は一度も評価されない: %s",
                                  i, static_cast<unsigned>(node.children[0]), path.c_str());
                }

                // 従に「currentActionId を書く行動」があると、主より後に評価されるので
                // 主の技の要求を上書きし、体が主の技を出せない。
                for (size_t k = 1; k < node.children.size(); ++k)
                {
                    if (!SubtreeWritesCommand(asset, node.children[k])) continue;

                    CCL_LOG_ERROR(LogCategory::AI,
                                  "[BT] nodes[%zu] の並行ノードの従(nodes[%u])に、ボスの行動要求を"
                                  "書く技がある。従は主より後に評価されるので主の技の要求を上書きし、"
                                  "体が主の技を出せない。主へ移すこと: %s",
                                  i, static_cast<unsigned>(node.children[k]), path.c_str());
                }

                if (node.children.size() == 1)
                {
                    CCL_LOG_INFO(LogCategory::AI,
                                 "[BT] nodes[%zu] の並行ノードに子が1つしかない。"
                                 "この場合は何も並行していないので、外しても挙動は変わらない: %s",
                                 i, path.c_str());
                }
            }
        }

        // ---------------------------------------------------------
        // 3. 毎フレーム即 Success の枝
        // ---------------------------------------------------------

        // 選択系の子に毎フレーム即 Success を返す枝があると、そこから下が一度も評価されない。
        // 失敗して次の枝へ進むことも無いので、外からは固まって見える。
        if (node.type == BTNodeType::Selector || node.type == BTNodeType::WeightedSelector ||
            node.type == BTNodeType::UtilitySelector)
        {
            for (size_t k = 0; k + 1 < node.children.size(); ++k)
            {
                const BTNodeID child = node.children[k];
                if (SubtreeCanBlock(asset, child) || SubtreeHasCondition(asset, child)) continue;

                CCL_LOG_ERROR(LogCategory::AI,
                              "[BT] nodes[%zu] の %zu 番目の枝(nodes[%u])は毎フレーム即 Success を返す"
                              "（待機も条件も無い）。この枝より下は一度も評価されない: %s",
                              i, k, static_cast<unsigned>(child), path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 4. 割り込みの印
        // ---------------------------------------------------------

        // 印が「効かない場所」に付いていないか。付けたのに何も起きない状態は、
        // 木を見ても分からない（印は正しく保存されていて、ただ誰も見ないだけ）。
        for (size_t k = 0; k < node.children.size(); ++k)
        {
            const BTNodeID child = node.children[k];
            if (!asset.nodes[child].interrupt) continue;

            if (node.type != BTNodeType::Selector && node.type != BTNodeType::UtilitySelector)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%u] に割り込みの印が付いているが、親の nodes[%zu] が Selector / UtilitySelector ではない。"
                             "印は無視される（割り込みを見るのはこの2つだけ）: %s",
                             static_cast<unsigned>(child), i, path.c_str());
            }
            // 一番下の枝の検査は Selector だけ。UtilitySelector には上下が無く、どの子も割り込める
            else if (node.type == BTNodeType::Selector && k + 1 == node.children.size())
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%u] は一番下の枝なので、割り込みの印は意味を持たない"
                             "（押しのける相手が下に無い）: %s",
                             static_cast<unsigned>(child), path.c_str());
            }
            else if (!HasReadableGuard(asset, child))
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%u] に割り込みの印が付いているが、枝の先頭に条件が無いので"
                             "割り込んでよいかを判定できない（無条件の割り込みは許さない）。"
                             "Sequence の先頭に条件を置くこと: %s",
                             static_cast<unsigned>(child), path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 5. 自己中断の印
        // ---------------------------------------------------------

        // 4 と同じ理由で検査する。ただし「一番下の枝」の検査は無い。
        // 押しのけられるのではなく自分から降りるので、一番下の枝でも意味があるため。
        for (const BTNodeID child : node.children)
        {
            if (!asset.nodes[child].abortSelf) continue;

            if (node.type != BTNodeType::Selector && node.type != BTNodeType::UtilitySelector)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%u] に自己中断の印が付いているが、親の nodes[%zu] が Selector / UtilitySelector ではない。"
                             "印は無視される（見直すのはこの2つだけ）: %s",
                             static_cast<unsigned>(child), i, path.c_str());
            }
            else if (!HasReadableGuard(asset, child))
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%u] に自己中断の印が付いているが、枝の先頭に条件が無いので"
                             "続けてよいかを判定できない。Sequence の先頭に条件を置くこと: %s",
                             static_cast<unsigned>(child), path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 6. 陣形指示の撃ち忘れ
        // ---------------------------------------------------------

        // 陣形指示だけを置いてリリースを忘れると、形を作って帰るだけになる。
        // 形は組み上がるので見た目は正しく、当たらないことにだけ気づけない。

        // 後続の枝に任せる中継もあるので WARN に留める。
        if (node.type == BTNodeType::Sequence)
        {
            int lastFormation = -1;
            for (const BTNodeID child : node.children)
            {
                const BTNode &kid = asset.nodes[child];
                if (kid.type != BTNodeType::Action) continue;
                if (kid.actionOrConditionId == A_DroneStrike)
                {
                    lastFormation = -1;
                }
                else if (IsDroneAttackFormation(kid.actionOrConditionId))
                {
                    lastFormation = static_cast<int>(child);
                }
            }
            if (lastFormation >= 0)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の陣形指示 nodes[%u] の後ろに「ドローン：攻撃実行」が無い。"
                             "形は組むが一度も撃たない: %s",
                             i, static_cast<unsigned>(lastFormation), path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 7. 抽選の重み
        // ---------------------------------------------------------

        // 0以下の枝は候補に入らないので、置いた本人は「たまたま出ない」と
        // 思うだけで、一度も出ないことに気づけない。

        // UtilitySelector の子で採点ルールがあるものは weight を使わないので、代わりに優先度（その子が取れる最高点）を見る。
        const bool isLottery = (node.type == BTNodeType::WeightedSelector || node.type == BTNodeType::UtilitySelector);
        if (isLottery && !node.children.empty())
        {
            float total = 0.0f;
            for (const BTNodeID c : node.children)
            {
                const BTNode &child   = asset.nodes[c];
                const bool    hasRule = (node.type == BTNodeType::UtilitySelector && child.scoreRuleIndex >= 0);
                const float   w       = hasRule ? asset.scoreRules[child.scoreRuleIndex].base : child.weight;
                if (w > 0.0f)
                {
                    total += w;
                    continue;
                }

                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の抽選するセレクターの枝 nodes[%u] は重みが %.2f。"
                             "0以下の枝は抽選の候補に入らないので一度も選ばれない: %s",
                             i, static_cast<unsigned>(c), w, path.c_str());
            }
            if (total <= 0.0f)
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[BT] nodes[%zu] の抽選するセレクターは、全ての枝の重みが0以下。"
                              "候補が1つも無いので常に Failure を返す: %s",
                              i, path.c_str());
            }
        }

        // ---------------------------------------------------------
        // 8. 抽選できる子の数
        // ---------------------------------------------------------

        // 抽選の候補はスタック上の固定長配列に入れるので、上限を超えた分は何も言わずに抽選から漏れる。
        if (isLottery && node.children.size() > static_cast<size_t>(kMaxWeightedSelectorChildren))
        {
            CCL_LOG_ERROR(LogCategory::AI,
                          "[BT] nodes[%zu] の抽選するセレクターに子が %zu 個ある。%d 個までしか抽選されない"
                          "（超えたぶんの技は一生出ない）: %s",
                          i, node.children.size(), kMaxWeightedSelectorChildren, path.c_str());
        }

        // ---------------------------------------------------------
        // 9. 採点ルールの距離の並び
        // ---------------------------------------------------------

        // 最小と最大を入れ違えると得意な範囲が消え、どの距離でも満点にならない。
        // 点数が少し下がるだけで技は出るので、見た目では気づけない。
        if (node.scoreRuleIndex >= 0)
        {
            const BTScoreRule &r = asset.scoreRules[node.scoreRuleIndex];
            if (r.bestMin > r.bestMax || r.falloff < 0.0f)
            {
                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の採点ルールの距離が 得意 %.1f〜%.1f m・ぼかし %.1f m。"
                             "最小 ≦ 最大、ぼかし ≧ 0 にすること: %s",
                             i, r.bestMin, r.bestMax, r.falloff, path.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 種類の変換
// ---------------------------------------------------------------------------

BTNodeType BehaviorTreeLoader::StringToNodeType(const std::string &typeStr)
{
    if (typeStr == "Selector") return BTNodeType::Selector;
    if (typeStr == "Sequence") return BTNodeType::Sequence;
    if (typeStr == "Condition") return BTNodeType::Condition;
    if (typeStr == "Decorator") return BTNodeType::Decorator;
    if (typeStr == "WeightedSelector") return BTNodeType::WeightedSelector;
    if (typeStr == "Parallel") return BTNodeType::Parallel;
    if (typeStr == "UtilitySelector") return BTNodeType::UtilitySelector;

    // 知らない種別は Action として読む。ここで弾くと木が丸ごと無反応になるので、
    // 行動として読み込んでおき、おかしさは ValidateNodes に報告させる。
    return BTNodeType::Action;
}
