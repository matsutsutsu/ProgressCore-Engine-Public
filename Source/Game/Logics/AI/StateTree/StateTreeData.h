#pragma once
#include <cstdint>
#include <vector>

// 厳格な型付け
using StateID        = uint16_t;
using TaskID         = uint16_t;
using TransitionID   = uint16_t;
using ActionID       = uint16_t;

constexpr StateID InvalidStateID = 0xFFFF;

// タスクの実行タイミング
enum class StateTaskType : uint8_t {
    Enter = 0,
    Tick = 1,
    Exit = 2
};

// ---------------------------------------------------------
// 1. Task (8 bytes)
// ---------------------------------------------------------
struct StateTreeTask {
    ActionID actionId;
    StateTaskType executionType;
    uint8_t padding; // アライメント調整
    float parameter; // 待機時間などの汎用パラメータ
};

// ---------------------------------------------------------
// 2. Transition (4 bytes)
// ---------------------------------------------------------
struct StateTreeTransition {
    ActionID conditionId;
    StateID targetStateId; // 遷移先のインデックス
};

// ---------------------------------------------------------
// 3. State Node (10 bytes -> 12 bytes padding)
// DODの要。ポインタではなく、他の配列の「どこから何個分か」を持つ
// ---------------------------------------------------------
struct StateTreeNode {
    StateID parentId;

    uint16_t taskStartIndex;
    uint16_t taskCount;

    uint16_t transitionStartIndex;
    uint16_t transitionCount;
};

// ---------------------------------------------------------
// Asset (全Entityで共有される不変データ)
// ---------------------------------------------------------
struct StateTreeAsset {
    uint32_t assetId;
    std::vector<StateTreeNode> states;
    std::vector<StateTreeTask> tasks;
    std::vector<StateTreeTransition> transitions;
};