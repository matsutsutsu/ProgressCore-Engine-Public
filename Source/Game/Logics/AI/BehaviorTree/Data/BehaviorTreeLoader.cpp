/**
 * BTアセット（JSON）をランタイム用の BTAsset へ読み込む実装。
 *
 * 役割:
 *   エディタが書き出した JSON の nodes[] を、BTNode の配列へ変換する。
 *   行動・条件は保存キー（"A_BossSlash" などの文字列）で解決し、
 *   数値IDは旧アセット互換のフォールバックとしてのみ使う。
 *
 * なぜこの形か:
 *   数値IDを正にすると、IDを詰め直せない・JSONを開いても何の技か読めない・
 *   技を足すときに番号を取り合う、の3つが同時に起きる。名前を正にすると全部消える
 *   （経緯は ActionRegistry.h の冒頭にまとめてある）。
 *
 * 注意点:
 *   古い書式のアセットは、この読み込み時に新しい形へ読み替える
 *   （MigrateLegacyWait / MigrateLegacyCondition）。読み替わるのはメモリ上だけなので、
 *   ファイル自体を新形式にするにはBTエディタで開いて保存し直す必要がある。
 */
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

            // param は旧名 decoratorParam から引き継ぐ（Decorator しか値を持たなかった頃の名前）。
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

    // 現行の条件として解決済みなら何もしない（キーが正、数値IDは保険）。
    const LegacyConditionEntry* legacy = FindLegacyCondition(savedKey, node.actionOrConditionId);
    if (!legacy) return;

    node.actionOrConditionId = legacy->newId;
    node.param               = legacy->param;

    if (legacy->ambiguous) {
        // 「15〜40m」のような帯は1ノードでは表せない。上限だけを引き継いだので、
        // 単独で置かれていた場合は意味が広がっている。黙って変えると気づけないため
        // ファイル名つきで知らせる。
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


BTNodeType BehaviorTreeLoader::StringToNodeType(const std::string& typeStr) {
    if (typeStr == "Selector")  return BTNodeType::Selector;
    if (typeStr == "Sequence")  return BTNodeType::Sequence;
    if (typeStr == "Condition") return BTNodeType::Condition;
    if (typeStr == "Decorator") return BTNodeType::Decorator;
    if (typeStr == "RandomSelector") return BTNodeType::RandomSelector;
    return BTNodeType::Action;
}
