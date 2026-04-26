#pragma once
#include <cstdint>
// 再定義を避け、元のBehaviorTreeDataからActionIDの型を引っ張ってくる
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

struct ActionEntry {
    ActionID id;
    const char* name;
    const char* category;
};

inline constexpr ActionEntry g_ActionRegistry[] = {
    { 0,   "なし (None)", "None" },
    { 100, "近接攻撃 (Melee Attack)", "Attack" },
    { 101, "突進 (Charge Attack)",    "Attack" },
    { 102, "移動 (Move To Target)",   "Move"   },
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

inline constexpr ActionEntry g_ConditionRegistry[] = {
    { 0, "なし (None)", "None" },
    { 1, "HP が 50% 以下 (HP < 50%)",        "Health" },
    { 2, "ターゲットが近い (Target is Near)", "Range"  },
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);
