/**
 * @file ActionRegistry.h
 * @brief AIの行動・条件のIDと、エディタ表示用の辞書（Single Source of Truth）
 *
 * Drone Break 最小構成（BTActions_Boss.h / BossActionSystem と同期）:
 *   - Boss 実行層が対応するのは A_Move のみ（それ以外は Idle）
 *   - BT 側は Wait / ResetDamage / Move / Drone(2000〜2029) を登録
 *   - 条件は BTConditions.h の8種を維持
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

    inline constexpr ActionID A_DroneFirst = 2000;
    inline constexpr ActionID A_DroneLast  = 2029;
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
    { AI::A_DroneStrike, "ドローン：攻撃実行 (Strike/リリース)", "Drone" },

    // --- ドローン群 (Drone) ---
    { AI::A_DroneOrbit,        "ドローン：円陣展開 (Orbit Circle)",   "Drone" },
    { AI::A_DroneHighOrbitSnipe, "ドローン：上空狙撃 (High Orbit Snipe)", "Drone" },
    { AI::A_DroneDeathRing,    "ドローン：処刑の輪 (Death Ring)",     "Drone" },
    { AI::A_DroneAegisShield,  "ドローン：絶対防衛 (Aegis Shield)",   "Drone" },
    { AI::A_DroneHighOrbit,    "ドローン：上空待機 (High Orbit)",     "Drone" },
    { AI::A_DroneLowOrbit,     "ドローン：降下展開 (Low Orbit)",      "Drone" },
    { AI::A_DroneSpreadLockOn, "ドローン：拡散静止 (Spread LockOn)",  "Drone" },
    { AI::A_DroneAllCharge,    "ドローン：一斉突撃 (All Charge)",     "Drone" },
    { AI::A_DroneCloseGuard,   "ドローン：全方位バリア (Close Guard)","Drone" },
    { AI::A_DroneBarrierBurst, "ドローン：バリア爆発 (Barrier Burst)", "Drone" },
    { AI::A_DroneChargeTunnel, "ドローン：突進通路 (Charge Tunnel)",  "Drone" },
    { AI::A_DroneHoldPosition, "ドローン：位置固定 (Hold Position)",  "Drone" },
    { AI::A_DroneDirectionalCharge, "ドローン：陣形維持の一斉突撃",    "Drone" },
    { AI::A_DroneCycloneBurst, "ドローン：サイクロンバースト",        "Drone" },
    { AI::A_DroneHighLowOrbit, "ドローン：双層の輪廻 (HighLow Orbit)", "Drone" },
    { AI::A_DroneCrossHelix,   "ドローン：交差螺旋 (Cross Helix)",     "Drone" },
    { AI::A_DroneKamikazeSwarm,"ドローン：暴雨の号令 (Kamikaze Swarm)","Drone" },
    { AI::A_DroneHighLowCycloneBurst, "ドローン：双層の竜巻",          "Drone" },
    { AI::A_DroneSweepingBeam, "128機：絶望の秒針 (Sweeping Beam)",   "Drone" },
    { AI::A_DroneInfinityEight,"128機：輪廻の軌道 (Infinity Eight)",   "Drone" },
    { AI::A_DroneSwarmRiver,   "128機：暴虐の濁流 (Swarm River)",     "Drone" },
    { AI::A_DroneSideSlapWhip, "ドローン：側面ビンタ (Side Slap Whip)","Drone" },
    { AI::A_DroneInfinitySnipe,"128機：輪廻の狙撃 (Infinity Snipe)",   "Drone" },
    { AI::A_DroneGuillotineSlam, "ドローン：断頭台振り下ろし",        "Drone" },
    { AI::A_DroneClapSlam,     "ドローン：刃挟み込み (Clap Slam)",    "Drone" },
    { AI::A_DroneTsunamiWave,  "128機：津波 (Tsunami Wave)",          "Drone" },
    { AI::A_DroneMegaClapUpperFirst, "128機：多段挟み込み（上段先攻）", "Drone" },
    { AI::A_DroneMegaClapLowerFirst, "128機：多段挟み込み（下段先攻）", "Drone" },
    { AI::A_DroneMegaShieldChargeUpperFirst, "128機：メガ・シールドチャージ（上段先攻）", "Drone" },
    { AI::A_DroneMegaShieldChargeLowerFirst, "128機：メガ・シールドチャージ（下段先攻）", "Drone" },
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
