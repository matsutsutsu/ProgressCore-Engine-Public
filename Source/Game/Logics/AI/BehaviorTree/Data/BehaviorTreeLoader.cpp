// BTアセット（JSON）をランタイム用の BTAsset へ読み込む実装。
// 行動・条件は保存キー（"A_BossSlash" などの文字列）で解決し、数値IDは旧アセット
// 互換のフォールバックとしてのみ使う。
//
// 古い書式のアセットはここで新しい形へ読み替える。読み替わるのはメモリ上だけなので、
// ファイル自体を新形式にするにはBTエディタで開いて保存し直す必要がある。
#include "BehaviorTreeLoader.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Platform/Core/FileSystem.h"
#include <json.hpp>
#include "Game/Logics/AI/BehaviorTree/Data/ActionRegistry.h"

using json = nlohmann::json;


bool BehaviorTreeLoader::LoadFromJson(const std::string& path, BTAsset& outAsset) {
    const std::string jsonStr = FileSystem::ReadAllText(path);
    if (jsonStr.empty()) {
        CCL_LOG_ERROR(LogCategory::AI,
            "Failed to load BT file: %s (FileSystem returned empty)", path.c_str());
        return false;
    }

    nlohmann::json j;
    try {
        j = json::parse(jsonStr);
    }
    catch (const std::exception& e) {
        CCL_LOG_ERROR(LogCategory::AI, "JSON Parse Error in %s: %s", path.c_str(), e.what());
        return false;
    }

    outAsset.id = j.value("assetId", 0u);
    outAsset.nodes.clear();

    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (auto& jNode : j["nodes"]) {
            BTNode node;
            node.type = StringToNodeType(jNode.value("type", "Action"));
            node.actionOrConditionId = ResolveActionOrConditionId(&jNode, node.type, path);

            node.decoratorType = static_cast<BTDecoratorType>(jNode.value("decoratorType", 0));

            // decoratorParam は Decorator しか値を持たなかった頃の名前。
            node.param = jNode.value("param", jNode.value("decoratorParam", 0.0f));

            MigrateLegacyWait(node);
            MigrateLegacyCondition(node, jNode.value("condition", std::string()), path);

            if (jNode.contains("children") && jNode["children"].is_array()) {
                for (auto& childId : jNode["children"]) {
                    node.children.push_back(childId.get<BTNodeID>());
                }
            }

            outAsset.nodes.push_back(node);
        }
    }

    if (outAsset.nodes.empty()) {
        CCL_LOG_ERROR(LogCategory::AI,
            "BT file has no runtime nodes[]: %s (editor_nodes のみの可能性)", path.c_str());
        return false;
    }

    ValidateNodes(outAsset, path);

    CCL_LOG_SUCCESS(LogCategory::AI, "BT Asset Loaded: %s (Nodes: %zu)", path.c_str(), outAsset.nodes.size());
    return true;
}

ActionID BehaviorTreeLoader::ResolveActionOrConditionId(const void* jNodePtr, BTNodeType type,
                                                        const std::string& path)
{
    const json& jNode = *static_cast<const json*>(jNodePtr);

    const ActionID numericId = static_cast<ActionID>(jNode.value("actionOrConditionId", 0));

    const bool isCondition = (type == BTNodeType::Condition);
    const char* keyField   = isCondition ? "condition" : "action";
    if (!jNode.contains(keyField) || !jNode[keyField].is_string()) return numericId;

    const std::string key = jNode[keyField].get<std::string>();
    if (key.empty() || key == "None") return numericId;

    ActionID resolved = 0;
    const bool found = isCondition ? TryFindConditionIdByKey(key, resolved)
                                   : TryFindActionIdByKey(key, resolved);
    if (found) return resolved;

    CCL_LOG_WARN(LogCategory::AI,
        "BTの未知のキー \"%s\" (%s)。数値ID %u で読み込む", key.c_str(), path.c_str(), numericId);
    return numericId;
}


void BehaviorTreeLoader::MigrateLegacyWait(BTNode& node)
{
    if (node.type != BTNodeType::Action) return;

    // 旧 A_Wait0_5s(100) 〜 A_Wait10_0s(111)。順序がそのまま秒数だった。
    static constexpr float kLegacyWaitSeconds[] = {
        0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f
    };
    constexpr ActionID kFirst = 100;
    constexpr ActionID kLast  = 111;

    if (node.actionOrConditionId < kFirst || node.actionOrConditionId > kLast) return;

    node.param               = kLegacyWaitSeconds[node.actionOrConditionId - kFirst];
    node.actionOrConditionId = CCL::AI::A_Wait;
}


void BehaviorTreeLoader::MigrateLegacyCondition(BTNode& node, const std::string& savedKey,
                                                const std::string& path)
{
    if (node.type != BTNodeType::Condition) return;

    // 現行の条件として解決済みなら何もしない。
    const LegacyConditionEntry* legacy = FindLegacyCondition(savedKey, node.actionOrConditionId);
    if (!legacy) return;

    node.actionOrConditionId = legacy->newId;
    node.param               = legacy->param;

    if (legacy->ambiguous) {
        // 帯は1ノードでは表せず上限だけを引き継ぐので、単独で置かれていた場合は
        // 意味が広がる。黙って変えると気づけないのでファイル名つきで知らせる。
        CCL_LOG_WARN(LogCategory::AI,
            "[BT] 旧条件 \"%s\" を「距離 < %.1fm」へ読み替えた: %s "
            "（元は帯だったので、下限が必要なら条件ノードを2つに分けること）",
            legacy->key, legacy->param, path.c_str());
    }
    else {
        CCL_LOG_INFO(LogCategory::AI,
            "[BT] 旧条件 \"%s\" を param %.1f 付きの新条件へ読み替えた: %s",
            legacy->key, legacy->param, path.c_str());
    }
}


namespace {
/// 枝の中に「Running を返しうる行動」が1つでもあるか。
bool SubtreeCanBlock(const BTAsset& asset, BTNodeID nodeId)
{
    const BTNode& node = asset.nodes[nodeId];
    if (node.type == BTNodeType::Action && IsBlockingAction(node.actionOrConditionId))
        return true;
    for (BTNodeID c : node.children)
        if (SubtreeCanBlock(asset, c)) return true;
    return false;
}

/// 枝が条件を持つか（＝失敗して次の枝へ譲る可能性があるか）。
bool SubtreeHasCondition(const BTAsset& asset, BTNodeID nodeId)
{
    const BTNode& node = asset.nodes[nodeId];
    if (node.type == BTNodeType::Condition) return true;
    for (BTNodeID c : node.children)
        if (SubtreeHasCondition(asset, c)) return true;
    return false;
}
} // namespace


void BehaviorTreeLoader::ValidateNodes(const BTAsset& asset, const std::string& path)
{
    // 「木としては読めるが、置いた本人の意図どおりには絶対に動かない」形を潰す。
    // 評価はワーカースレッドで走るので、実行時にはログを出せない。
    for (size_t i = 0; i < asset.nodes.size(); ++i) {
        const BTNode& node = asset.nodes[i];

        if (node.type == BTNodeType::Decorator) {
            // クールダウン0秒＝毎フレーム通過。新規作成の param は 0 なので、
            // 「Cooldown を選んだが待機時間を触っていない」だけでこうなる。
            if (node.decoratorType == BTDecoratorType::Cooldown && node.param <= 0.0f) {
                CCL_LOG_WARN(LogCategory::AI,
                    "[BT] nodes[%zu] のクールダウンが %.2f 秒。0以下は毎フレーム素通りする"
                    "（＝クールダウンが効かない）: %s",
                    i, node.param, path.c_str());
            }
            // 試行回数0＝子を一度も評価せず、常に Failure になる。
            if (node.decoratorType == BTDecoratorType::Retry && node.param < 1.0f) {
                CCL_LOG_WARN(LogCategory::AI,
                    "[BT] nodes[%zu] のリトライ回数が %.0f。1未満は子を評価できない: %s",
                    i, node.param, path.c_str());
            }
            if (node.children.empty()) {
                CCL_LOG_WARN(LogCategory::AI,
                    "[BT] nodes[%zu] のデコレーターに子が繋がっていない: %s", i, path.c_str());
            }
        }

        // 選択系の子に「毎フレーム即 Success を返す枝」があると、そこから下が死ぬ。
        // 失敗して譲ることも無いので、外からは固まって見える。
        if (node.type == BTNodeType::Selector || node.type == BTNodeType::RandomSelector) {
            for (size_t k = 0; k + 1 < node.children.size(); ++k) {
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
        if (node.type == BTNodeType::Sequence) {
            int lastFormation = -1;
            for (const BTNodeID child : node.children) {
                const BTNode& kid = asset.nodes[child];
                if (kid.type != BTNodeType::Action) continue;
                if (kid.actionOrConditionId == CCL::AI::A_DroneStrike) {
                    lastFormation = -1;
                }
                else if (IsDroneAttackFormation(kid.actionOrConditionId)) {
                    lastFormation = static_cast<int>(child);
                }
            }
            if (lastFormation >= 0) {
                CCL_LOG_WARN(LogCategory::AI,
                    "[BT] nodes[%zu] の陣形指示 nodes[%u] の後ろに「ドローン：攻撃実行」が無い。"
                    "形は組むが一度も撃たない: %s",
                    i, static_cast<unsigned>(lastFormation), path.c_str());
            }
        }

        // 抽選バッファはスタック固定長。超えたぶんは黙って抽選から漏れる。
        if (node.type == BTNodeType::RandomSelector &&
            node.children.size() > static_cast<size_t>(kMaxRandomSelectorChildren)) {
            CCL_LOG_ERROR(LogCategory::AI,
                "[BT] nodes[%zu] の RandomSelector に子が %zu 個ある。%d 個までしか抽選されない"
                "（超えたぶんの技は一生出ない）: %s",
                i, node.children.size(), kMaxRandomSelectorChildren, path.c_str());
        }
    }
}


BTNodeType BehaviorTreeLoader::StringToNodeType(const std::string& typeStr) {
    if (typeStr == "Selector")  return BTNodeType::Selector;
    if (typeStr == "Sequence")  return BTNodeType::Sequence;
    if (typeStr == "Condition") return BTNodeType::Condition;
    if (typeStr == "Decorator") return BTNodeType::Decorator;
    if (typeStr == "RandomSelector") return BTNodeType::RandomSelector;
    return BTNodeType::Action;
}
