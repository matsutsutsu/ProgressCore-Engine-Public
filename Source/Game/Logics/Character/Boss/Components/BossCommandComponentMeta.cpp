// BossCommandComponent のシリアライズ/インスペクタ定義。
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h" // kDroneFormationNames
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <>
struct ComponentMeta<BossCommandComponent>
{
    CCL_META_USING_THIS(BossCommandComponent)

    static constexpr const char *displayName       = "Boss Command (AI Output)";
    static constexpr const char *category          = CCL::Categories::AI;
    static constexpr int         sortPriority      = 160;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            // どれも実行時の値。保存しない。
            CFIELD_RT(currentActionId, "現在のActionID", "Current Command")
            CFIELD_RT(actionSerial, "技の依頼番号", "Current Command")
            CFIELD_ENUM_U8_RT(requestFormation, "フォーメーション要求", kDroneFormationNames.data(), kDroneFormationNames.size(), "Drone Control")};
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossCommandComponent)
