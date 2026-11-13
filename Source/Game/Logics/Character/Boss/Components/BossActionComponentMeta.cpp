#include "BossActionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// BossActionState と1対1で並べる
static const char *BossStateNames[] = {
    "None",
    "Move",
    "Reposition",
    "BackStep",
    "Leap",
    "Dash",
    "Footwork",
    "Exhausted",
};

// BossFootworkDir と1対1で並べる
static const char *BossFootworkDirNames[] = {
    "None",    // 様子見していない
    "Forward", // 前
    "Back",    // 後ろ
    "Left",    // 左
    "Right",   // 右
};

// BossRepositionPhase と1対1で並べる
static const char *BossRepositionPhaseNames[] = {
    "Start",  // 動き出し
    "Travel", // 一定の速さで進む
    "Stop",   // 止まる
    "End",    // 止まり終えた
};

// BossLeapLanding と1対1で並べる
static const char *BossLeapLandingNames[] = {
    "AwayToCenter", // 中央を挟んで相手と反対側
    "TowardTarget", // 相手の手前
    "InPlace",      // その場
};

// BossLeapPhase と1対1で並べる
static const char *BossLeapPhaseNames[] = {
    "Windup",   // 溜め
    "Flight",   // 空中
    "Hover",    // 頂点で停止
    "Descent",  // 頂点から降りる
    "Recovery", // 着地硬直
};

// BossBackStepPhase と1対1で並べる
static const char *BossBackStepPhaseNames[] = {
    "Retreat",  // 下がる
    "Stop",     // 止まる
    "Recovery", // 着地硬直
};

// BossDashPhase と1対1で並べる
static const char *BossDashPhaseNames[] = {
    "Retreat",  // 後ろへ下がる
    "Aim",      // 止まって狙う
    "Windup",   // 構え（方向は固定済み）
    "Charge",   // 突っ込む
    "Travel",   // 走り続ける
    "Stop",     // 止まる
    "Recovery", // 硬直
};

template <>
struct ComponentMeta<BossActionComponent>
{
    CCL_META_USING_THIS(BossActionComponent)

    static constexpr const char *displayName       = "Boss Action";
    static constexpr const char *category          = CCL::Categories::Combat;
    static constexpr int         sortPriority      = 180;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(turnSpeed, "旋回速度(移動中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(idleTurnSpeed, "旋回速度(待機中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(repositionSpeed, "間合い：最高速 [m/s]（距離は BT のノードで指定）", 0.5f, 1.0f, 180.0f, "Reposition")
            CFIELD_FLOAT_RANGED(repositionAccelTime, "間合い：加速の時間 [秒]", 0.01f, 0.0f, 2.0f, "Reposition")
            CFIELD_FLOAT_RANGED(repositionDecelTime, "間合い：減速の時間 [秒]", 0.01f, 0.0f, 2.0f, "Reposition")
            CFIELD_FLOAT_RANGED(repositionNearDistance, "間合い：これより近いと離れる [m]", 0.5f, 0.0f, 180.0f, "Reposition")
            CFIELD_FLOAT_RANGED(repositionFarDistance, "間合い：これより遠いと詰める [m]", 0.5f, 0.0f, 180.0f, "Reposition")
            CFIELD_FLOAT_RANGED(repositionWallDistance, "間合い：壁がこれより近いと内側へ寄る [m]", 0.5f, 0.0f, 90.0f, "Reposition")
            CFIELD_FLOAT_RANGED(footworkSpeed, "様子見：動き回る速さ [m/s]", 0.5f, 0.0f, 120.0f, "Footwork")
            CFIELD_FLOAT_RANGED(footworkMinTime, "様子見：1回の長さの下限 [秒]", 0.05f, 0.0f, 10.0f, "Footwork")
            CFIELD_FLOAT_RANGED(footworkMaxTime, "様子見：1回の長さの上限 [秒]", 0.05f, 0.0f, 10.0f, "Footwork")
            CFIELD_FLOAT_RANGED(backStepDistance, "後退：下がる距離 [m]", 0.1f, 0.0f, 60.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepRetreatTime, "後退：下がり始めの時間 [秒]（この後に止まりのモーション）", 0.01f, 0.0f, 3.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepRecovery, "後退：着地硬直 [秒]", 0.01f, 0.0f, 3.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepHeight, "後退：浮く高さ [m]", 0.1f, 0.0f, 10.0f, "Back Step")
            CFIELD_FLOAT_RANGED(escapeLeap.windup, "中央へ逃げる：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLeap.flight, "中央へ逃げる：空中 [秒]", 0.01f, 0.1f, 5.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLeap.landRecovery, "中央へ逃げる：着地硬直 [秒]", 0.01f, 0.0f, 3.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLeap.height, "中央へ逃げる：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Escape")
            CFIELD_ENUM_U8(escapeLeap.landing, "中央へ逃げる：着地点の決め方", BossLeapLandingNames, std::size(BossLeapLandingNames), "Leap Escape")
            CFIELD(escapeLeap.landAttack, "中央へ逃げる：着地で攻撃する", "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLandingRatio, "中央へ逃げる：着地点（半径に対する割合）", 0.01f, 0.0f, 0.9f, "Leap Escape")
            CFIELD_FLOAT_RANGED(attackLeap.windup, "跳躍攻撃：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackLeap.flight, "跳躍攻撃：空中 [秒]", 0.01f, 0.1f, 5.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackLeap.landRecovery, "跳躍攻撃：着地硬直 [秒]", 0.01f, 0.0f, 3.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackLeap.height, "跳躍攻撃：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Attack")
            CFIELD_ENUM_U8(attackLeap.landing, "跳躍攻撃：着地点の決め方", BossLeapLandingNames, std::size(BossLeapLandingNames), "Leap Attack")
            CFIELD(attackLeap.landAttack, "跳躍攻撃：着地で攻撃する", "Leap Attack")
            CFIELD_FLOAT_RANGED(attackStandOff, "跳躍攻撃：相手の手前に降りる距離 [m]", 0.1f, 0.0f, 20.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(hoverLeap.windup, "滞空：溜め [秒]", 0.01f, 0.0f, 2.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.flight, "滞空：上昇＋下降 [秒]", 0.01f, 0.1f, 5.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.hover, "滞空：頂点に留まる [秒]", 0.05f, 0.0f, 30.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.landRecovery, "滞空：着地硬直 [秒]", 0.01f, 0.0f, 3.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.height, "滞空：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Air Hover")
            CFIELD_ENUM_U8(hoverLeap.landing, "滞空：着地点の決め方", BossLeapLandingNames, std::size(BossLeapLandingNames), "Air Hover")
            CFIELD(hoverLeap.landAttack, "滞空：着地で攻撃する", "Air Hover")
            CFIELD_FLOAT_RANGED(dashRetreatTime, "突進：下がる時間 [秒]", 0.01f, 0.05f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashRetreatDistance, "突進：下がる距離 [m]", 0.1f, 0.0f, 30.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashRetreatHeight, "突進：浮く高さ [m]", 0.1f, 0.0f, 20.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashAimTime, "突進：狙う時間（相手を向き続ける）[秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashWindupTime, "突進：溜め（方向固定・避ける猶予）[秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashSpeed, "突進：突っ込む速さ [m/s]", 0.5f, 1.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashDistance, "突進：突っ込む距離 [m]", 0.5f, 1.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashTravelSpeed, "突進：着地後に走る速さ [m/s]", 0.5f, 0.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashTravelTime, "突進：着地後に走り続ける時間 [秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashStopTime, "突進：止まるまでの時間 [秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashRecovery, "突進：止まった後の硬直 [秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(flinchCooldown, "ひるみ：明けてから次にひるめるまで [秒]", 0.05f, 0.0f, 30.0f, "Flinch")
            CFIELD_FLOAT_RANGED(exhaustedDuration, "疲れ：その場で止まる長さ [秒]（怒りの連発の後の反撃の時間）", 0.05f, 0.0f, 15.0f, "Exhausted")
            CFIELD_FLOAT_RANGED(rageRecoveryScale, "怒り：突進・跳躍攻撃の硬直の倍率（1 で普段どおり）", 0.01f, 0.0f, 1.0f, "Rage")
            // ここから下は実行時の値。保存しない
            CFIELD_ENUM_U8_RT(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
            CFIELD_RT(lastActionTime, "前の技の長さ [秒]（溜めから硬直まで）", "State")
            CFIELD_RT(actionFinished, "今の技が終わったか", "State")
            CFIELD_ENUM_U8_RT(repositionPhase, "間合いを取るの段", BossRepositionPhaseNames, std::size(BossRepositionPhaseNames), "State")
            CFIELD_RT(repositionCruiseTime, "間合い：最高速で進む長さ [秒]", "State")
            CFIELD_RT(footworkTime, "様子見：今回の長さ [秒]", "State")
            CFIELD_ENUM_U8_RT(footworkAnimDir, "様子見：アニメの向き", BossFootworkDirNames, std::size(BossFootworkDirNames), "State")
            CFIELD_ENUM_U8_RT(leapPhase, "跳躍の段", BossLeapPhaseNames, std::size(BossLeapPhaseNames), "State")
            CFIELD_ENUM_U8_RT(backStepPhase, "後退の段", BossBackStepPhaseNames, std::size(BossBackStepPhaseNames), "State")
            CFIELD_RT(backStepStopTime, "後退：止まりのモーションの長さ [秒]（アニメグラフから）", "State")
            CFIELD_ENUM_U8_RT(dashPhase, "突進の段", BossDashPhaseNames, std::size(BossDashPhaseNames), "State")
            CFIELD_RT(dashChargeTime, "突進：今回の突っ込む長さ [秒]（距離÷速さ）", "State")
            CFIELD_RT(isFlinching, "ひるんでいるか", "State")
            CFIELD_RT(flinchCooldownTimer, "ひるみ：次にひるめるまで [秒]", "State")};
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)
