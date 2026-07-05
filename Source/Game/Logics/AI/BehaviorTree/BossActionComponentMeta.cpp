/**
 * @file BossActionComponentMeta.cpp
 * @brief BossActionComponent (筋肉) のシリアライズ/インスペクタ定義。
 */
#include "BossActionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

static const char* BossStateNames[] = {
    "None", "Move", "Melee", "Charge", "JumpAttack", "Evade", "Flinch","JumpToHover", "HoverToLand",
};

template <> struct ComponentMeta<BossActionComponent> {
    CCL_META_USING_THIS(BossActionComponent) 

    static constexpr const char* displayName    = "Boss Action (Muscle)";
    static constexpr const char* category       = CCL::Categories::Combat; // ★追加
    static constexpr int         sortPriority   = 180; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = false;
    static constexpr bool        isSerializable = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(walkSpeed, "歩行速度", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(chargeSpeed, "突進速度", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(turnSpeed, "旋回速度", 0.1f, 0.0f, 100.0f, "Stats")
            CFIELD_FLOAT_RANGED(evadeSpeed, "回避速度", 0.1f, 0.0f, 100.0f, "Stats")

            CFIELD_FLOAT_RANGED(currentMoveSpeed, "現在の移動速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(acceleration, "加速度", 0.1f, 0.0f, 100.0f, "Inertia")
            CFIELD_FLOAT_RANGED(deceleration, "減速度", 0.1f, 0.0f, 100.0f, "Inertia")

            CFIELD_FLOAT_RANGED(jumpAttackApexHeight, "ジャンプ攻撃の頂点の高さ", 0.1f, 0.0f, 100.0f, "Visuals")
            CFIELD_FLOAT_RANGED(chargeHoverVelocity, "チャージ前の浮遊初速", 0.1f, 0.0f, 100.0f, "Visuals")

            CFIELD_FLOAT_RANGED(currentChargeTotal, "チャージ攻撃の合計時間", 0.1f, 0.0f, 100.0f, "Charge Attack Timing")
            CFIELD_FLOAT_RANGED(currentChargeAnimStart, "チャージ攻撃のアニメ開始時間", 0.1f, 0.0f, 100.0f, "Charge Attack Timing")

            CFIELD_FLOAT_RANGED(hoverAltitude, "ホバリング高度", 0.5f, 0.0f, 100.0f, "Visuals")
            CFIELD(isHovering, "ホバリング中フラグ", "State")

            CFIELD_ENUM_U8(currentState, "現在の状態", BossStateNames, std::size(BossStateNames), "State")
            CFIELD_FLOAT_RANGED(actionTimer, "硬直タイマー", 0.1f, 0.0f, 100.0f, "State")
        };
        return fields;
    }
};

REGISTER_COMPONENT(BossActionComponent, "BossActionComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossActionComponent)