// BossActionComponent (筋肉) のシリアライズ/インスペクタ定義。最小構成版。
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
};

// BossLeapPhase と1対1で並べる。
static const char *BossLeapPhaseNames[] = {
    "Windup",
    "Flight",
    "Recovery",
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
                    CFIELD_FLOAT_RANGED(advanceStopDistance, "詰めるのを止める距離 [m]", 0.5f, 0.0f, 60.0f, "Stats")
                        CFIELD_FLOAT_RANGED(acceleration, "加速度", 0.1f, 0.0f, 100.0f, "Inertia")
                            CFIELD_FLOAT_RANGED(deceleration, "減速度", 0.1f, 0.0f, 100.0f, "Inertia")
                                    CFIELD_FLOAT_RANGED(backStepSpeed, "後退：踏み切り速度 [m/s]", 0.5f, 0.0f, 100.0f, "Back Step")
                CFIELD_FLOAT_RANGED(backStepTime, "後退：後退時間 [秒]", 0.01f, 0.01f, 2.0f, "Back Step")
                CFIELD_FLOAT_RANGED(backStepHeight, "後退：跡の高さ [m]", 0.1f, 0.0f, 10.0f, "Back Step")
                CFIELD_FLOAT_RANGED(escapeWindupTime, "中央へ逃げる：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Escape")
                CFIELD_FLOAT_RANGED(escapeFlightTime, "中央へ逃げる：滞空 [秒]", 0.01f, 0.1f, 5.0f, "Leap Escape")
                CFIELD_FLOAT_RANGED(escapeHeight, "中央へ逃げる：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Escape")
                CFIELD_FLOAT_RANGED(escapeLandingRatio, "中央へ逃げる：着地点（半径に対する割合）", 0.01f, 0.0f, 0.9f, "Leap Escape")
                CFIELD_FLOAT_RANGED(attackWindupTime, "跳躍攻撃：溜め [秒]", 0.01f, 0.0f, 2.0f, "Leap Attack")
                CFIELD_FLOAT_RANGED(attackFlightTime, "跳躍攻撃：滞空 [秒]", 0.01f, 0.1f, 5.0f, "Leap Attack")
                CFIELD_FLOAT_RANGED(attackHeight, "跳躍攻撃：頂点の高さ [m]", 0.1f, 0.0f, 60.0f, "Leap Attack")
                CFIELD_FLOAT_RANGED(attackStandOff, "跳躍攻撃：相手の手前に降りる距離 [m]", 0.1f, 0.0f, 20.0f, "Leap Attack")
            // ここから下は書き手が BossActionSystem の実行時の値。保存しない。
            //   保存すると「たまたま保存した瞬間の技」がプレハブへ焼き付き、
            //   次に生成された個体がその状態で生まれてくる。
            CFIELD_RT(currentMoveSpeed, "現在の移動速度", "State")
                CFIELD_ENUM_U8_RT(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
                CFIELD_ENUM_U8_RT(leapPhase, "跳躍の段", BossLeapPhaseNames, std::size(BossLeapPhaseNames), "State")};
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)