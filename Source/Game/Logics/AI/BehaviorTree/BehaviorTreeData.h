#pragma once
#include <cstdint>
#include <vector>

namespace CCL::AI {

    // 厳格な型付け
    using BTNodeID = uint16_t;
    using ActionID = uint16_t;
    using AssetID = uint32_t;

    enum class BTNodeType : uint8_t {
        Selector,
        Sequence,
        Condition,
        Action
    };

    enum class BTNodeState : uint8_t {
        Success,
        Failure,
        Running
    };

    // 1つのノード定義。メモリレイアウトを意識し、8バイトに収める。
    struct BTNode {
        BTNodeType type;
        uint8_t    childCount;          // 子ノードの数
        BTNodeID   firstChildOffset;    // 最初の子ノードへの配列インデックス
        ActionID   actionOrConditionId; // Action/Conditionの種類を判別するID
    };

    // 全Entityで共有される不変データ（アセット）
    struct BTAsset {
        AssetID id;
        std::vector<BTNode> nodes;
    };

} // namespace CCL::AI