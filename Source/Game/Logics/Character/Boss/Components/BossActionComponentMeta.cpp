/**
 * BossActionComponent (筋肉) のシリアライズ/インスペクタ定義。最小構成版。
 */
#include "BossActionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

static const char* BossStateNames[] = {
    "None", "Move",
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
            CFIELD_FLOAT_RANGED(runSpeed, "走行速度（行動用）", 0.1f, 0.0f, 100.0f, "Action Speed")
            CFIELD_FLOAT_RANGED(chargeSpeed, "突進速度（行動用）", 0.1f, 0.0f, 100.0f, "Action Speed")
            CFIELD_FLOAT_RANGED(turnSpeed, "旋回速度(移動中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(idleTurnSpeed, "旋回速度(待機中)", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(acceleration, "加速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(deceleration, "減速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(currentMoveSpeed, "現在の移動速度", 0.1f, 0.0f, 100.0f, "State")
            CFIELD_ENUM_U8(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
        };
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)