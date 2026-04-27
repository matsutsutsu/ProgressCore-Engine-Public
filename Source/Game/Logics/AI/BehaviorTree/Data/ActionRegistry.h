#pragma once
#include <cstdint>
// 再定義を避け、元のBehaviorTreeDataからActionIDの型を引っ張ってくる
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

struct ActionEntry {
    ActionID id;
    const char* name;
    const char* category;
};

// ★ 修正：ドローン制御とWaitアクションを追加
inline constexpr ActionEntry g_ActionRegistry[] = {
    { 0,   "なし (None)", "None" },
    { 100, "近接攻撃 (Melee Attack)", "Attack" },
    { 101, "突進 (Charge Attack)",    "Attack" },
    { 102, "移動 (Move To Target)",   "Move"   },
    { 103, "ドローン：円陣展開 (Drone Orbit)", "Drone" },
    { 104, "ドローン：順番突撃 (Drone Sequential)", "Drone" },
    { 105, "待機 1秒 (Wait 1s)", "Common" },
    { 106, "待機 3秒 (Wait 3s)", "Common" },
    { 107, "ドローン：処刑の輪 (Death Ring)", "Drone" },
    { 108, "ドローン：絶対防衛 (Aegis Shield)", "Drone" },
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

// ★ 修正：フェーズ移行の条件を追加
inline constexpr ActionEntry g_ConditionRegistry[] = {
    { 0, "なし (None)", "None" },
    { 1, "HP が 50% 以下 (HP < 50%)",        "Health" },
    { 2, "ターゲットが近い (Target is Near)", "Range"  },
    { 3, "フェーズ 2 (後半戦) (Phase >= 2)",  "State"  },
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);