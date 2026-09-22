// BTアセット（JSON）をランタイム用の BTAsset へ読み込む実装。
//
// 行動・条件は保存キー（"A_BossLeapAttack" などの文字列）で解決する。キーにしてあるのは、
// 数値IDだと辞書の並びを変えた瞬間に保存済みのアセットが別の技を指すため。
// アセットには数値IDも併記されていて、キーを引けなかったときだけそちらを使う。
#include "BehaviorTreeLoader.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <json.hpp>
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"

#include <cstdint>
#include <utility>
#include <vector>

using json = nlohmann::json;

bool BehaviorTreeLoader::LoadFromJson(const std::string &path, BTAsset &outAsset)
{
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

    outAsset.id = j.value("assetId", 0u);
    outAsset.nodes.clear();

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

            if (jNode.contains("children") && jNode["children"].is_array())
            {
                for (auto &childId : jNode["children"])
                {
                    node.children.push_back(childId.get<BTNodeID>());
                }
            }

            outAsset.nodes.push_back(node);
        }
    }

    if (outAsset.nodes.empty())
    {
        CCL_LOG_ERROR(LogCategory::AI,
                      "BT file has no runtime nodes[]: %s (editor_nodes のみの可能性)", path.c_str());
        return false;
    }

    // 構造が壊れている木は、この先の ValidateNodes が範囲外を読む。必ず先に通す。
    if (!ValidateStructure(outAsset, path))
    {
        outAsset.nodes.clear();
        return false;
    }

    ValidateNodes(outAsset, path);

    CCL_LOG_SUCCESS(LogCategory::AI, "BT Asset Loaded: %s (Nodes: %zu)", path.c_str(), outAsset.nodes.size());
    return true;
}

ActionID BehaviorTreeLoader::ResolveActionOrConditionId(const void *jNodePtr, BTNodeType type,
                                                        const std::string &path)
{
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

namespace
{
    /// 枝の中に「Running を返しうる行動」が1つでもあるか。
    bool SubtreeCanBlock(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action && IsBlockingAction(node.actionOrConditionId))
            return true;
        for (BTNodeID c : node.children)
            if (SubtreeCanBlock(asset, c)) return true;
        return false;
    }

    /// 枝の中に「currentActionId を書く行動」が1つでもあるか。
    /// Parallel の従に置くと、要求が下りないまま残る（ActionRegistry.h に理由）。
    bool SubtreeWritesCommand(const BTAsset &asset, BTNodeID nodeId)
    {
        const BTNode &node = asset.nodes[nodeId];
        if (node.type == BTNodeType::Action && IsCommandWritingAction(node.actionOrConditionId))
            return true;
        for (BTNodeID c : node.children)
            if (SubtreeWritesCommand(asset, c)) return true;
        return false;
    }

    /// 枝が条件を持つか（＝失敗して次の枝へ譲る可能性があるか）。
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
    const size_t n = asset.nodes.size();

    // --- 1) 子インデックスの範囲 ---
    //   1つでも範囲外があると、この直後の ValidateNodes が asset.nodes[child] を
    //   そのまま読む。std::vector::operator[] なので検出もされない。
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

    // --- 2) 循環 ---
    //   評価(EvaluateTreeRecursive)も検証(SubtreeCanBlock)も再帰で降りるので、
    //   1周でもあるとスタックが尽きて落ちる。
    //   ここ自体は再帰を使わない。壊れた入力から守る側が同じ穴に落ちては意味がない。
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

    // --- 3) root(0) から辿り着けないノード ---
    //   落ちはしないが、置いたのに繋ぎ忘れた枝がここで分かる。
    //   （デバッグの「強制実行」だけは任意のノードから始められるので ERROR にはしない）
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
    // 「木としては読めるが、置いた本人の意図どおりには絶対に動かない」形を潰す。
    // 評価はワーカースレッドで走るので、実行時にはログを出せない。
    for (size_t i = 0; i < asset.nodes.size(); ++i)
    {
        const BTNode &node = asset.nodes[i];

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

        // 並行ノードの検査。主(children[0])と従(それ以降)で意味が違うので、
        // 置き間違えると「従が一度も動かない」「技が終わっても体が止まらない」に
        // なる。どちらも実行中は静かなので、ここで言う。
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

                // 従に「currentActionId を書く行動」があると、主が終わった後
                // その要求が下りないまま残り、BossActionSystem が技を続けてしまう。
                for (size_t k = 1; k < node.children.size(); ++k)
                {
                    if (!SubtreeWritesCommand(asset, node.children[k])) continue;

                    CCL_LOG_ERROR(LogCategory::AI,
                                  "[BT] nodes[%zu] の並行ノードの従(nodes[%u])に、ボスの行動要求を"
                                  "書く技がある。主が終わると従は評価されなくなるので要求が下りず、"
                                  "技が終わっても体がその技を続ける。主へ移すこと: %s",
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

        // 選択系の子に「毎フレーム即 Success を返す枝」があると、そこから下が死ぬ。
        // 失敗して譲ることも無いので、外からは固まって見える。
        if (node.type == BTNodeType::Selector || node.type == BTNodeType::WeightedSelector)
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

        // 陣形指示だけを置いてリリースを忘れると、形を作って帰るだけになる。形は
        // 組み上がるので見た目は正しく、当たらないことにだけ気づけない。
        // 後続の枝に任せる中継もあるので WARN に留める。
        if (node.type == BTNodeType::Sequence)
        {
            int lastFormation = -1;
            for (const BTNodeID child : node.children)
            {
                const BTNode &kid = asset.nodes[child];
                if (kid.type != BTNodeType::Action) continue;
                if (kid.actionOrConditionId == CCL::AI::A_DroneStrike)
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

        // 抽選バッファはスタック固定長。超えたぶんは黙って抽選から漏れる。
        // 重みの検査。0以下の枝は候補に入らないので、置いた本人は「たまたま出ない」と
        // 思うだけで、一生出ないことに気づけない。
        if (node.type == BTNodeType::WeightedSelector && !node.children.empty())
        {
            float total = 0.0f;
            for (const BTNodeID c : node.children)
            {
                const float w = asset.nodes[c].weight;
                if (w > 0.0f)
                {
                    total += w;
                    continue;
                }

                CCL_LOG_WARN(LogCategory::AI,
                             "[BT] nodes[%zu] の重み付きセレクターの枝 nodes[%u] は重みが %.2f。"
                             "0以下の枝は抽選の候補に入らないので一度も選ばれない: %s",
                             i, static_cast<unsigned>(c), w, path.c_str());
            }
            if (total <= 0.0f)
            {
                CCL_LOG_ERROR(LogCategory::AI,
                              "[BT] nodes[%zu] の重み付きセレクターは、全ての枝の重みが0以下。"
                              "候補が1つも無いので常に Failure を返す: %s",
                              i, path.c_str());
            }
        }

        if (node.type == BTNodeType::WeightedSelector &&
            node.children.size() > static_cast<size_t>(kMaxWeightedSelectorChildren))
        {
            CCL_LOG_ERROR(LogCategory::AI,
                          "[BT] nodes[%zu] の重み付きセレクターに子が %zu 個ある。%d 個までしか抽選されない"
                          "（超えたぶんの技は一生出ない）: %s",
                          i, node.children.size(), kMaxWeightedSelectorChildren, path.c_str());
        }
    }
}

BTNodeType BehaviorTreeLoader::StringToNodeType(const std::string &typeStr)
{
    if (typeStr == "Selector") return BTNodeType::Selector;
    if (typeStr == "Sequence") return BTNodeType::Sequence;
    if (typeStr == "Condition") return BTNodeType::Condition;
    if (typeStr == "Decorator") return BTNodeType::Decorator;
    if (typeStr == "WeightedSelector") return BTNodeType::WeightedSelector;
    if (typeStr == "Parallel") return BTNodeType::Parallel;

    // 知らない種別は Action として読む。ここで弾くと木が丸ごと無反応になるので、
    // 行動として読み込んでおき、おかしさは ValidateNodes に報告させる。
    return BTNodeType::Action;
}
