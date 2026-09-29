// BossCommandComponent のシリアライズ/インスペクタ定義。
//
// 狙う相手はここに持たない。AIBlackboard::targetEntity（BossPerceptionSystem が毎フレーム決める）が持つ。
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h" // kDroneFormationNames
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
            // どのフィールドも BT が実行中に書く値なので、保存しない。
            //   保存すると、保存した瞬間の技や陣形がプレハブに残り、ボスが技の途中の状態から始まる。
            //   インスペクタでは見たいので、保存だけを止める。
            CFIELD_RT(currentActionId, "現在のActionID", "Current Command")
            CFIELD_RT(currentActionDuration, "現在の技の全体時間 [秒]", "Current Command")
            CFIELD_ENUM_U8_RT(requestFormation, "フォーメーション要求", kDroneFormationNames.data(), kDroneFormationNames.size(), "Drone Control")};
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossCommandComponent)
