// BossActionComponent のシリアライズ/インスペクタ定義。
#include "BossActionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// BossActionState と1対1で並べる（順番がずれると別の状態名が表示される）。
static const char *BossStateNames[] = {
    "None",
    "Move",
    "BackStep",
    "Leap",
    "Dash",
};

// BossLeapPhase と1対1で並べる。
static const char *BossLeapPhaseNames[] = {
    "Windup",   // 溜め
    "Flight",   // 空中
    "Hover",    // 頂点で停止
    "Recovery", // 着地硬直
};

// BossDashPhase と1対1で並べる。
static const char *BossDashPhaseNames[] = {
    "Retreat",  // 後ろへ下がる
    "Aim",      // 止まって狙う
    "Charge",   // 突っ込む
    "Travel",   // 走り続ける
    "Stop",     // 止まる
    "Recovery", // 硬直
};

template <>
struct ComponentMeta<BossActionComponent>
{
    CCL_META_USING_THIS(BossActionComponent)

    static constexpr const char *displayName       = "Boss Action (Muscle)";
    static constexpr const char *category          = CCL::Categories::Combat;
    static constexpr int         sortPriority      = 180;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(walkSpeed, "歩行速度（行動用）", 0.1f, 0.0f, 100.0f, "Action Speed")
            CFIELD_FLOAT_RANGED(turnSpeed, "旋回速度(移動中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(idleTurnSpeed, "旋回速度(待機中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(advanceStopDistance, "詰めるのを止める距離 [m]", 0.5f, 0.0f, 60.0f, "Stats")
            CFIELD_FLOAT_RANGED(acceleration, "加速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(deceleration, "減速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(backStepSpeed, "後退：踏み切り速度 [m/s]", 0.5f, 0.0f, 100.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepTime, "後退：後退時間 [秒]", 0.01f, 0.01f, 2.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepHeight, "後退：弧の高さ [m]", 0.1f, 0.0f, 10.0f, "Back Step")
            CFIELD_FLOAT_RANGED(escapeLeap.windup, "中央へ逃げる：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLeap.flight, "中央へ逃げる：滞空 [秒]", 0.01f, 0.1f, 5.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLeap.height, "中央へ逃げる：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Escape")
            CFIELD_FLOAT_RANGED(escapeLandingRatio, "中央へ逃げる：着地点（半径に対する割合）", 0.01f, 0.0f, 0.9f, "Leap Escape")
            CFIELD_FLOAT_RANGED(attackLeap.windup, "跳躍攻撃：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackLeap.flight, "跳躍攻撃：滞空 [秒]", 0.01f, 0.1f, 5.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackLeap.height, "跳躍攻撃：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(attackStandOff, "跳躍攻撃：相手の手前に降りる距離 [m]", 0.1f, 0.0f, 20.0f, "Leap Attack")
            CFIELD_FLOAT_RANGED(hoverLeap.windup, "滞空：溜め [秒]", 0.01f, 0.0f, 2.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.flight, "滞空：上昇＋下降 [秒]", 0.01f, 0.1f, 5.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLandRecovery, "滞空：着地硬直 [秒]（頂点の長さはBTノードの全体時間から逆算）", 0.01f, 0.0f, 3.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(hoverLeap.height, "滞空：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Air Hover")
            CFIELD_FLOAT_RANGED(dashRetreatTime, "突進：下がる時間 [秒]", 0.01f, 0.05f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashRetreatDistance, "突進：下がる距離 [m]", 0.1f, 0.0f, 30.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashRetreatHeight, "突進：浮く高さ [m]", 0.1f, 0.0f, 20.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashAimTime, "突進：狙う時間（予告）[秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashSpeed, "突進：突っ込む速さ [m/s]", 0.5f, 1.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashDistance, "突進：突っ込む距離 [m]", 0.5f, 1.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashTravelSpeed, "突進：着地後に走る速さ [m/s]", 0.5f, 0.0f, 150.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashTravelTime, "突進：走り続ける時間 [秒]", 0.01f, 0.0f, 5.0f, "Dash Attack")
            CFIELD_FLOAT_RANGED(dashStopTime, "突進：止まるまでの時間 [秒]", 0.01f, 0.0f, 3.0f, "Dash Attack")
            // ここから下は BossActionSystem が書く実行時の値。保存しない。
            //   保存すると、保存した瞬間の技の状態がプレハブに残り、
            //   次に生成された個体がその状態から始まる。
            CFIELD_RT(currentMoveSpeed, "現在の移動速度", "State")
            CFIELD_ENUM_U8_RT(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
            CFIELD_ENUM_U8_RT(leapPhase, "跳躍の段", BossLeapPhaseNames, std::size(BossLeapPhaseNames), "State")
            CFIELD_ENUM_U8_RT(dashPhase, "突進の段", BossDashPhaseNames, std::size(BossDashPhaseNames), "State")};
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)