#pragma once
#include <vector>
#include <memory>
#include "Game/Logics/AI/BehaviorTree/BehaviorTreeData.h"

namespace CCL::AI {

    class BehaviorTreeBuilder {
    private:
        struct EditorNode {
            BTNodeType type;
            ActionID actionOrConditionId = 0;
            std::vector<std::unique_ptr<EditorNode>> children;
            EditorNode* parent = nullptr;
        };

        std::unique_ptr<EditorNode> m_root;
        EditorNode* m_currentNode = nullptr;

        void AddNode(BTNodeType type, ActionID id);

    public:
        BehaviorTreeBuilder();

        BehaviorTreeBuilder& Selector();
        BehaviorTreeBuilder& Sequence();
        BehaviorTreeBuilder& Condition(ActionID conditionId);
        BehaviorTreeBuilder& Action(ActionID actionId);
        BehaviorTreeBuilder& End();

        void Bake(BTAsset& outAsset);
    };

} // namespace CCL::AI