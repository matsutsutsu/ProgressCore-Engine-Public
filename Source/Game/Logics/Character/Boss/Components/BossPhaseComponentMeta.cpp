/**
 * @file BossPhaseComponentMeta.cpp
 * @brief BossPhaseComponent のシリアライズ/インスペクタ定義。
 */
#include "BossPhaseComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// 表示名は BossPhaseComponent.h の kBossPhaseNames と共有する（二重管理を避ける）

template <> struct ComponentMeta<BossPhaseComponent> {
    CCL_META_USING_THIS(BossPhaseComponent)

    static constexpr const char* displayName       = "Boss Phase (大状態)";
    static constexpr const char* category          = CCL::Categories::Combat;
    static constexpr int         sortPriority      = 175;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_ENUM_U8(phase, "現在の大状態", kBossPhaseNames, kBossPhaseCount, "State")
            CFIELD_FLOAT_RANGED(timer, "状態滞在タイマー", 0.1f, 0.0f, 60.0f, "State")
        };
        return fields;
    }
};

REGISTER_COMPONENT(BossPhaseComponent, "BossPhaseComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossPhaseComponent)
