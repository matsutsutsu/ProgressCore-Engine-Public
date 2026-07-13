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
            node.actionOrConditionId = static_cast<ActionID>(jNode.value("actionOrConditionId", 0));

            node.decoratorType = static_cast<BTDecoratorType>(jNode.value("decoratorType", 0));
            node.decoratorParam = jNode.value("decoratorParam", 0.0f);

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

BTNodeType BehaviorTreeLoader::StringToNodeType(const std::string& typeStr) {
    if (typeStr == "Selector")  return BTNodeType::Selector;
    if (typeStr == "Sequence")  return BTNodeType::Sequence;
    if (typeStr == "Condition") return BTNodeType::Condition;
    if (typeStr == "Decorator") return BTNodeType::Decorator;
    if (typeStr == "RandomSelector") return BTNodeType::RandomSelector;
    return BTNodeType::Action;
}

std::string BehaviorTreeLoader::DumpTreeToString(const BTAsset& asset) {
    if (asset.nodes.empty()) return "[Empty Tree]";

    std::string result = "Behavior Tree Dump (Asset ID: " + std::to_string(asset.id) + ")\n";
    result += "=================================================\n";
    result += DumpNodeRecursive(asset, 0, "", true);
    result += "=================================================\n";
    return result;
}

std::string BehaviorTreeLoader::DumpNodeRecursive(const BTAsset& asset, BTNodeID nodeId, const std::string& prefix, bool isLast) {
    std::string result;
    const BTNode& node = asset.nodes[nodeId];

    if (nodeId != 0) {
        result += prefix;
        result += isLast ? " └─ " : " ├─ ";
    }

    std::string nodeTypeStr;
    switch (node.type) {
    case BTNodeType::Selector:  nodeTypeStr = "[Selector]"; break;
    case BTNodeType::Sequence:  nodeTypeStr = "[Sequence]"; break;
    case BTNodeType::Condition: nodeTypeStr = "[Condition]"; break;
    case BTNodeType::Action:    nodeTypeStr = "[Action]"; break;
    case BTNodeType::Decorator: nodeTypeStr = "[Decorator]"; break;
    case BTNodeType::RandomSelector: nodeTypeStr = "[RandomSelector]"; break;
    }
    result += nodeTypeStr;

    if (node.type == BTNodeType::Action) {
        std::string actionName = "Unknown";
        for (int i = 0; i < g_ActionRegistryCount; ++i) {
            if (g_ActionRegistry[i].id == node.actionOrConditionId) {
                actionName = g_ActionRegistry[i].name;
                break;
            }
        }
        result += " " + actionName;
    }
    else if (node.type == BTNodeType::Condition) {
        std::string condName = "Unknown";
        for (int i = 0; i < g_ConditionRegistryCount; ++i) {
            if (g_ConditionRegistry[i].id == node.actionOrConditionId) {
                condName = g_ConditionRegistry[i].name;
                break;
            }
        }
        result += " " + condName;
    }
    else if (node.type == BTNodeType::Decorator) {
        switch (node.decoratorType) {
        case BTDecoratorType::Inverter: result += " (Invert)"; break;
        case BTDecoratorType::Cooldown: result += " (Cooldown: " + std::to_string(node.decoratorParam) + "s)"; break;
        case BTDecoratorType::Retry:    result += " (Retry: " + std::to_string((int)node.decoratorParam) + " times)"; break;
        case BTDecoratorType::Async:    result += " (Async)"; break;
        }
    }

    result += "\n";

    std::string childPrefix = prefix;
    if (nodeId != 0) {
        childPrefix += isLast ? "     " : " │   ";
    }

    for (size_t i = 0; i < node.children.size(); ++i) {
        bool childIsLast = (i == node.children.size() - 1);
        result += DumpNodeRecursive(asset, node.children[i], childPrefix, childIsLast);
    }

    return result;
}
