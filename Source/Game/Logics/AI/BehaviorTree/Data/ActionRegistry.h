/**
 * @file ActionRegistry.h
 * @brief AIの行動・条件のIDと、エディタ表示用の辞書（Single Source of Truth）
 */
#pragma once
#include <cstdint>
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

 // ============================================================================
 // 【単一情報源】AIの「行動」と「条件」のID定義
 // ============================================================================
namespace AI {
    // ==========================================
    // 【条件 (Conditions: 1〜99)】
    // ==========================================
    // --- 距離・位置 ---
    inline constexpr ActionID C_RangeClose = 1;  // 距離が近い
    inline constexpr ActionID C_RangeMedium = 2;  // 距離が中程度
    inline constexpr ActionID C_RangeMostClose = 5;  // 距離が至近距離
    inline constexpr ActionID C_RangeFar = 7;    // 距離が遠い

    // --- 状態・フェーズ ---
    inline constexpr ActionID C_IsPhase2 = 3;  // フェーズ2以降か
    // 第2形態への移行条件を満たしているか
    inline constexpr ActionID C_ShouldTransitionPhase2 = 4;

	inline constexpr ActionID C_DamageThresholdExceeded = 6; //　蓄積ダメージが閾値を超えたか

	inline constexpr ActionID C_IsAtStageEdge = 8; //　ステージの端にいるか    

    // ==========================================
    // 【行動 (Actions)】 桁飛ばし（ID Spacing）による再設計
    // ==========================================

    // --- 100番台: 汎用・待機 ---
    inline constexpr ActionID A_Wait0_5s  =  100;
    inline constexpr ActionID A_Wait1_0s  =  101;
    inline constexpr ActionID A_Wait1_5s  =  102;
    inline constexpr ActionID A_Wait2_0s  =  103;
    inline constexpr ActionID A_Wait3_0s  =  104;
    inline constexpr ActionID A_Wait4_0s  =  105; 
    inline constexpr ActionID A_Wait5_0s  =  106;
    inline constexpr ActionID A_Wait6_0s  =  107; 
    inline constexpr ActionID A_Wait7_0s  =  108; 
    inline constexpr ActionID A_Wait8_0s  =  109; 
    inline constexpr ActionID A_Wait9_0s  =  110; 
    inline constexpr ActionID A_Wait10_0s =  111;


    // --- 1000番台: ボス本体の移動・攻撃 ---
    inline constexpr ActionID A_Move = 1000;
    inline constexpr ActionID A_MeleeAttack = 1001;
    inline constexpr ActionID A_ChargeAttack = 1002;
    inline constexpr ActionID A_JumpAttack = 1003;
    inline constexpr ActionID A_EvadeBackward = 1004;
    inline constexpr ActionID A_ResetDamage = 1005;
    inline constexpr ActionID A_PhaseTransition = 1006;
    inline constexpr ActionID A_JumpToHover = 1007;
    inline constexpr ActionID A_HoverToLand = 1008;
    // 横移動アクション
    inline constexpr ActionID A_StrafeLeft = 1009;
    inline constexpr ActionID A_StrafeRight = 1010;
    inline constexpr ActionID A_StrafeRandom = 1011;

    inline constexpr ActionID A_EscapeToCenter = 1012;

    // --- 2000番台: ドローン制御 ---
    inline constexpr ActionID A_DroneOrbit = 2000;
    inline constexpr ActionID A_DroneHighOrbitSnipe = 2001;
    inline constexpr ActionID A_DroneDeathRing = 2002;
    inline constexpr ActionID A_DroneAegisShield = 2003;
    inline constexpr ActionID A_DroneHighOrbit = 2004;
    inline constexpr ActionID A_DroneLowOrbit = 2005;
    inline constexpr ActionID A_DroneSpreadLockOn = 2006;
    inline constexpr ActionID A_DroneAllCharge = 2007;
    inline constexpr ActionID A_DroneCloseGuard = 2008;
    inline constexpr ActionID A_DroneBarrierBurst = 2009;
    inline constexpr ActionID A_DroneChargeTunnel = 2010;
    inline constexpr ActionID A_DroneHoldPosition = 2011;
    inline constexpr ActionID A_DroneDirectionalCharge = 2012;
    inline constexpr ActionID A_DroneCycloneBurst = 2013;


    // 第2形態用・新規フォーメーション
    inline constexpr ActionID A_DroneHighLowOrbit = 2014;
    inline constexpr ActionID A_DroneCrossHelix = 2015;
    inline constexpr ActionID A_DroneKamikazeSwarm = 2016;
    inline constexpr ActionID A_DroneHighLowCycloneBurst = 2017;

    inline constexpr ActionID A_DroneSweepingBeam = 2018;
    inline constexpr ActionID A_DroneInfinityEight = 2019;
    inline constexpr ActionID A_DroneSwarmRiver = 2020;

    inline constexpr ActionID A_DroneSideSlapWhip = 2021;

    inline constexpr ActionID A_DroneInfinitySnipe = 2022; // インフィニティ展開＆クロスファイア狙撃

    inline constexpr ActionID A_DroneGuillotineSlam = 2023; // 縦割り攻撃
    inline constexpr ActionID A_DroneClapSlam = 2024; // 挟み込み攻撃

    inline constexpr ActionID A_DroneTsunamiWave = 2025; // 瀑布の津波（上から降りて押し寄せる波）

    inline constexpr ActionID A_DroneMegaClapUpperFirst = 2026; // 多段挟み込み（上優先）
    inline constexpr ActionID A_DroneMegaClapLowerFirst = 2027; // 多段挟み込み（下優先）

    //  メガ・シールドチャージのID (番号は他と被らないようにしてください)
    inline constexpr ActionID A_DroneMegaShieldChargeUpperFirst = 2028;
    inline constexpr ActionID A_DroneMegaShieldChargeLowerFirst = 2029;


}

// ============================================================================
// エディタUI表示用の辞書（上のIDと必ずセットで書く）
// ※この配列の並び順が、エディタのプルダウンメニューの順番になります
// ============================================================================
struct ActionEntry {
    ActionID id;
    const char* name;
    const char* category;
};

// 行動(Action)の辞書
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

    // --- ボス本体 (Boss) ---
    { AI::A_Move,         "ボス：移動 (Move To Target)", "Boss" },
    { AI::A_MeleeAttack,  "ボス：近接攻撃 (Melee Attack)", "Boss" },
    { AI::A_ChargeAttack, "ボス：突進 (Charge Attack)",    "Boss" },
    { AI::A_JumpAttack,   "ボス：ジャンプ攻撃 (Jump Attack)", "Boss" },
	{ AI::A_EvadeBackward,"ボス：後方回避 (Evade Backward)", "Boss" },
    { AI::A_ResetDamage,  "ボス：蓄積ダメージをリセット (Reset Damage)", "Boss" },
    { AI::A_PhaseTransition, "ボス：フェーズ転換（Phase Transition）","Boss"},
    { AI::A_JumpToHover,  "ボス：上空へジャンプ＆滞空 (Jump To Hover)", "Boss" },
    { AI::A_HoverToLand,  "ボス：プレイヤーへ急降下着地 (Hover To Land)", "Boss" },
    { AI::A_StrafeLeft,   "ボス：左へ回り込む (Strafe Left)", "Boss" },
    { AI::A_StrafeRight,  "ボス：右へ回り込む (Strafe Right)", "Boss" },
    { AI::A_StrafeRandom, "ボス：ランダムな方向へ回り込む (Strafe Random)", "Boss" },
    { AI::A_EscapeToCenter, "ボス：中央へ回避 (Escape To Center)", "Boss" },

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
    { AI::A_DroneBarrierBurst,"ドローン：バリア爆発 (Barrier Burst)", "Drone" }, 
	{ AI::A_DroneChargeTunnel, "ドローン：突進通路 (Charge Tunnel)", "Drone" },
    { AI::A_DroneHoldPosition, "ドローン：位置固定(待機)", "Drone" },
    { AI::A_DroneDirectionalCharge, "ドローン：陣形維持の一斉突撃", "Drone" },
    { AI::A_DroneCycloneBurst, "ドローン：サイクロンバースト(回転放射)", "Drone" },
    { AI::A_DroneSideSlapWhip, "ドローン：側面ビンタ (Side Slap Whip)", "Drone" },
    { AI::A_DroneGuillotineSlam, "ドローン：断頭台振り下ろし (Guillotine Slam)", "Drone" },
    { AI::A_DroneClapSlam, "ドローン：刃挟み込み (Clap Slam)", "Drone" },

    // 第2形態用・新規フォーメーション
    { AI::A_DroneHighLowOrbit, "ドローン：双層の輪廻 (HighLow Orbit)", "Drone" },
    { AI::A_DroneHighLowCycloneBurst, "ドローン：双層の竜巻 (HighLow CycloneBurst)", "Drone" },
    { AI::A_DroneCrossHelix,   "ドローン：交差螺旋 (Cross Helix)",     "Drone" },
    { AI::A_DroneKamikazeSwarm,"ドローン：暴雨の号令 (Kamikaze Swarm)","Drone" },
    
    { AI::A_DroneSweepingBeam, "128ドローン：絶望の秒針 (Sweeping Beam)", "Drone" },
    { AI::A_DroneInfinityEight,"128ドローン：輪廻の軌道 (Infinity Eight)","Drone" },
    { AI::A_DroneInfinitySnipe,"128ドローン：輪廻の狙撃 (Infinity Snipe)","Drone" },
    { AI::A_DroneSwarmRiver,   "128ドローン：暴虐の濁流 (Swarm River)",   "Drone" },
    { AI::A_DroneTsunamiWave,   "128ドローン：津波 (Wave)",   "Drone" },
    { AI::A_DroneMegaClapUpperFirst, "128ドローン：多段挟み込み（上段先攻）", "Drone" },
    { AI::A_DroneMegaClapLowerFirst, "128ドローン：多段挟み込み（下段先攻）", "Drone" },
    { AI::A_DroneMegaShieldChargeUpperFirst, "128ドローン：メガ・シールドチャージ（上段先攻）", "Drone" },
	{ AI::A_DroneMegaShieldChargeLowerFirst, "128ドローン：メガ・シールドチャージ（下段先攻）", "Drone" },


};
inline constexpr int g_ActionRegistryCount = sizeof(g_ActionRegistry) / sizeof(ActionEntry);

// 条件(Condition)の辞書
inline constexpr ActionEntry g_ConditionRegistry[] = {
    { 0, "なし (None)", "None" },
    { AI::C_RangeClose,  "距離 < 15m",    "Range" },
    { AI::C_RangeMedium, "距離 15-40m",   "Range" },
    { AI::C_RangeMostClose, "距離 < 8m",  "Range" },
    { AI::C_IsPhase2,    "フェーズ >= 2", "State" },
    { AI::C_ShouldTransitionPhase2, "条件：第2形態への移行条件 (HP半分以下 & 第1形態)", "Condition" }, 
	{ AI::C_DamageThresholdExceeded, "ダメージ蓄積 > 閾値", "State" },
    { AI::C_RangeFar,    "距離 > 40m",    "Range" }, 
	{ AI::C_IsAtStageEdge, "ステージの端にいる", "State" },
};
inline constexpr int g_ConditionRegistryCount = sizeof(g_ConditionRegistry) / sizeof(ActionEntry);