// BossActionComponent (筋肉) のシリアライズ/インスペクタ定義。最小構成版。
#include "BossActionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// BossActionState と1対1で並べる（順番がずれると別の状態名が表示される）。
static const char* BossStateNames[] = {
    "None", "Move", "Melee", "DashSlash", "JumpAttack", "BackStep",
};

template <> struct ComponentMeta<BossActionComponent> {
    CCL_META_USING_THIS(BossActionComponent)

    static constexpr const char* displayName    = "Boss Action (Muscle)";
    static constexpr const char* category       = CCL::Categories::Combat;
    static constexpr int         sortPriority   = 180;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(walkSpeed, "歩行速度（行動用）", 0.1f, 0.0f, 100.0f, "Action Speed")
            CFIELD_FLOAT_RANGED(turnSpeed, "旋回速度(移動中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(acceleration, "加速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(deceleration, "減速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(chargeSpeed, "突進速度", 0.5f, 0.0f, 200.0f, "Dash Slash")
            CFIELD_FLOAT_RANGED(dashWindupTime, "突進：溜め [秒]", 0.01f, 0.0f, 3.0f, "Dash Slash")
            CFIELD_FLOAT_RANGED(dashDriveTime, "突進：踏み込み [秒]", 0.01f, 0.0f, 3.0f, "Dash Slash")
            CFIELD_FLOAT_RANGED(jumpWindupTime, "跳躍：溜め [秒]", 0.01f, 0.0f, 3.0f, "Jump Attack")
            CFIELD_FLOAT_RANGED(jumpFlightTime, "跳躍：滞空 [秒]", 0.01f, 0.05f, 3.0f, "Jump Attack")
            CFIELD_FLOAT_RANGED(jumpHeight, "跳躍：頂点の高さ [m]", 0.1f, 0.0f, 30.0f, "Jump Attack")
            CFIELD_FLOAT_RANGED(jumpMaxRange, "跳躍：最大水平距離 [m]", 0.5f, 0.0f, 60.0f, "Jump Attack")
            CFIELD_FLOAT_RANGED(backStepSpeed, "後退：踏み切り速度 [m/s]", 0.5f, 0.0f, 100.0f, "Back Step")
            CFIELD_FLOAT_RANGED(backStepTime, "後退：後退時間 [秒]", 0.01f, 0.01f, 2.0f, "Back Step")
            // ここから下は書き手が BossActionSystem の実行時の値。保存しない。
            //   保存すると「たまたま保存した瞬間の技」がプレハブへ焼き付き、
            //   次に生成された個体がその状態で生まれてくる。
            CFIELD_RT(currentMoveSpeed, "現在の移動速度", "State")
            CFIELD_ENUM_U8_RT(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
        };
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)