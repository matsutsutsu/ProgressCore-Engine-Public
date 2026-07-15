/**
 * @file ActionRegistry.h
 * @brief AIの行動・条件のIDと、エディタ表示用の辞書（Single Source of Truth）
 *
 * Drone Break 最小構成（BTActions_Boss.h / BossActionSystem と同期）:
 *   - Boss 実行層が対応するのは A_Move のみ（それ以外は Idle）
 *   - BT 側は Wait / ResetDamage / Move / Drone(2000〜2029) を登録
 *   - 条件は BTConditions.h の8種を維持
 *
 * ★ g_ActionRegistry（BTWindowのAction候補リスト）はDrone系を「実装済みのみ」に
 *   絞ってある（OrbitCircle / GuillotineSlam / DroneStrike の3つ）。
 *   AI:: 側の定数と DroneAction の登録ループ(2000〜2029)自体は残しているので、
 *   Formation実装（Formations_Basic.h / Formations_Attack.h）が増えたら
 *   g_ActionRegistry に1行追加するだけで候補に復活する。
 */
#pragma once
#include <cstdint>
#include <cstring>
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

// ============================================================================
// 【単一情報源】AIの「行動」と「条件」のID定義
// ============================================================================
namespace AI {
    // ==========================================
    // 【条件 (Conditions: 1〜99)】
    // ==========================================
    inline constexpr ActionID C_RangeClose              = 1;
    inline constexpr ActionID C_RangeMedium             = 2;
    inline constexpr ActionID C_IsPhase2                = 3;
    inline constexpr ActionID C_ShouldTransitionPhase2  = 4;
    inline constexpr ActionID C_RangeMostClose          = 5;
    inline constexpr ActionID C_DamageThresholdExceeded = 6;
    inline constexpr ActionID C_RangeFar                = 7;
    inline constexpr ActionID C_IsAtStageEdge             = 8;

    // ==========================================
    // 【行動 (Actions)】 桁飛ばし（ID Spacing）
    // ==========================================

    // --- 100番台: 汎用・待機 ---
    inline constexpr ActionID A_Wait0_5s  = 100;
    inline constexpr ActionID A_Wait1_0s  = 101;
    inline constexpr ActionID A_Wait1_5s  = 102;
    inline constexpr ActionID A_Wait2_0s  = 103;
    inline constexpr ActionID A_Wait3_0s  = 104;
    inline constexpr ActionID A_Wait4_0s  = 105;
    inline constexpr ActionID A_Wait5_0s  = 106;
    inline constexpr ActionID A_Wait6_0s  = 107;
    inline constexpr ActionID A_Wait7_0s  = 108;
    inline constexpr ActionID A_Wait8_0s  = 109;
    inline constexpr ActionID A_Wait9_0s  = 110;
    inline constexpr ActionID A_Wait10_0s = 111;

    // --- 1000番台: ボス本体（最小構成）---
    inline constexpr ActionID A_Move        = 1000; ///< BossActionSystem が実行
    inline constexpr ActionID A_ResetDamage = 1005; ///< ブラックボードの蓄積ダメージをリセット
    inline constexpr ActionID A_DroneStrike = 1006; ///< ドローンへ「今撃て」信号（陣形攻撃のリリース）

    // --- 2000番台: ドローン制御（id - 2000 + 1 → DroneFormationType）---
    inline constexpr ActionID A_DroneOrbit        = 2000;
    inline constexpr ActionID A_DroneHighOrbitSnipe = 2001;
    inline constexpr ActionID A_DroneDeathRing    = 2002;
    inline constexpr ActionID A_DroneAegisShield  = 2003;
    inline constexpr ActionID A_DroneHighOrbit    = 2004;
    inline constexpr ActionID A_DroneLowOrbit     = 2005;
    inline constexpr ActionID A_DroneSpreadLockOn = 2006;
    inline constexpr ActionID A_DroneAllCharge    = 2007;
    inline constexpr ActionID A_DroneCloseGuard   = 2008;
    inline constexpr ActionID A_DroneBarrierBurst = 2009;
    inline constexpr ActionID A_DroneChargeTunnel = 2010;
    inline constexpr ActionID A_DroneHoldPosition = 2011;
    inline constexpr ActionID A_DroneDirectionalCharge = 2012;
    inline constexpr ActionID A_DroneCycloneBurst = 2013;
    inline constexpr ActionID A_DroneHighLowOrbit = 2014;
    inline constexpr ActionID A_DroneCrossHelix   = 2015;
    inline constexpr ActionID A_DroneKamikazeSwarm = 2016;
    inline constexpr ActionID A_DroneHighLowCycloneBurst = 2017;
    inline constexpr ActionID A_DroneSweepingBeam = 2018;
    inline constexpr ActionID A_DroneInfinityEight = 2019;
    inline constexpr ActionID A_DroneSwarmRiver   = 2020;
    inline constexpr ActionID A_DroneSideSlapWhip = 2021;
    inline constexpr ActionID A_DroneInfinitySnipe = 2022;
    inline constexpr ActionID A_DroneGuillotineSlam = 2023;
    inline constexpr ActionID A_DroneClapSlam     = 2024;
    inline constexpr ActionID A_DroneTsunamiWave  = 2025;
    inline constexpr ActionID A_DroneMegaClapUpperFirst = 2026;
    inline constexpr ActionID A_DroneMegaClapLowerFirst = 2027;
    inline constexpr ActionID A_DroneMegaShieldChargeUpperFirst = 2028;
    inline constexpr ActionID A_DroneMegaShieldChargeLowerFirst = 2029;
    inline constexpr ActionID A_DroneTsunamiWaveRL = 2030; ///< 波状縦割り（右→左）。enum TsunamiWave_RL に対応

    inline constexpr ActionID A_DroneFirst = 2000;
    inline constexpr ActionID A_DroneLast  = 2030;
}

// ============================================================================
// エディタUI表示用の辞書
// ============================================================================
struct ActionEntry {
    ActionID    id;
    const char* name;
    const char* category;
};

inline constexpr ActionEntry g_ActionRegistry[] = {
    { 0, "なし (None)", "None" },

    // --- 汎用・待機 (Common) ---
    { AI::A_Wait0_5s,  "待機: 0.5秒",  "Common" },
    { AI::A_Wait1_0s,  "待機: 1.0秒",  "Common" },
    { AI::A_Wait1_5s,  "待機: 1.5秒",  "Common" },
    { AI::A_Wait2_0s,  "待機: 2.0秒",  "Common" },
    { AI::A_Wait3_0s,  "待機: 3.0秒",  "Common" },
    { AI::A_Wait4_0s,  "待機: 4.0秒",  "Common" },
    { AI::A_Wait5_0s,  "待機: 5.0秒",  "Common" },
    { AI::A_Wait6_0s,  "待機: 6.0秒",  "Common" },
    { AI::A_Wait7_0s,  "待機: 7.0秒",  "Common" },
    { AI::A_Wait8_0s,  "待機: 8.0秒",  "Common" },
    { AI::A_Wait9_0s,  "待機: 9.0秒",  "Common" },
    { AI::A_Wait10_0s, "待機: 10.0秒", "Common" },

    // --- ボス本体 (Boss) ※実行層は Move のみ対応 ---
    { AI::A_Move,        "ボス：移動 (Move To Player)", "Boss" },
    { AI::A_ResetDamage, "ボス：蓄積ダメージリセット",   "Boss" },

    // --- ドローン群 (Drone) ※候補は「実装済みのみ」に絞ってある。
    //     AI:: 側の定数・DroneAction registration(2000〜2029) は温存しているので、
    //     Formation実装が増えたらここへ1行足すだけで候補に復活する。
    { AI::A_DroneOrbit,          "ドローン：円陣展開 (Orbit Circle) ※帰投にも使用", "Drone" },
    { AI::A_DroneGuillotineSlam, "ドローン：縦割り (Guillotine Slam)", "Drone" },
    { AI::A_DroneClapSlam,       "ドローン：挟み込み1段 (Clap Slam)", "Drone" },
    { AI::A_DroneKamikazeSwarm,  "ドローン：弾幕 (Kamikaze Swarm)", "Drone" },
    { AI::A_DroneMegaClapLowerFirst, "ドローン：挟み込み2段・下段先攻 (Mega Clap)", "Drone" },
    { AI::A_DroneMegaClapUpperFirst, "ドローン：挟み込み2段・上段先攻 (Mega Clap)", "Drone" },
    { AI::A_DroneSideSlapWhip,   "ドローン：横なぎ回転 (Side Slap Whip)", "Drone" },
    { AI::A_DroneTsunamiWave,    "ドローン：波状縦割り・左→右 (Wave)", "Drone" },
    { AI::A_DroneTsunamiWaveRL,  "ドローン：波状縦割り・右→左 (Wave)", "Drone" },
    { AI::A_DroneStrike,         "ドローン：攻撃実行 (Strike/リリース)", "Drone" },
};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

inline constexpr ActionEntry g_ConditionRegistry[] = {
    { 0, "なし (None)", "None" },
    { AI::C_RangeClose,              "距離 < 15m",    "Range" },
    { AI::C_RangeMedium,             "距離 15-40m",   "Range" },
    { AI::C_RangeMostClose,          "距離 < 8m",     "Range" },
    { AI::C_RangeFar,                "距離 > 40m",    "Range" },
    { AI::C_IsPhase2,                "フェーズ >= 2", "State" },
    { AI::C_ShouldTransitionPhase2,  "第2形態移行条件 (HP半分以下 & 第1形態)", "State" },
    { AI::C_DamageThresholdExceeded, "ダメージ蓄積 > 閾値", "State" },
    { AI::C_IsAtStageEdge,           "ステージの端にいる", "State" },
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);

// ============================================================================
// エディタ / ランタイム共通ヘルパー
// ============================================================================
inline const ActionEntry* FindActionEntry(ActionID id)
{
    for (int i = 0; i < g_ActionRegistryCount; ++i) {
        if (g_ActionRegistry[i].id == id) return &g_ActionRegistry[i];
    }
    return nullptr;
}

inline const ActionEntry* FindConditionEntry(ActionID id)
{
    for (int i = 0; i < g_ConditionRegistryCount; ++i) {
        if (g_ConditionRegistry[i].id == id) return &g_ConditionRegistry[i];
    }
    return nullptr;
}

inline bool IsWaitAction(ActionID id)
{
    return id >= AI::A_Wait0_5s && id <= AI::A_Wait10_0s;
}

inline bool IsDroneAction(ActionID id)
{
    return id >= AI::A_DroneFirst && id <= AI::A_DroneLast;
}

inline bool IsBossPhysicalAction(ActionID id)
{
    return id == AI::A_Move;
}
