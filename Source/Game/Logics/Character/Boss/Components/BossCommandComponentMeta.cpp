// BossCommandComponent のシリアライズ/インスペクタ定義。
//
// targetPlayerId は撤去した。シリアライズ欄はあったが、コード上どこからも
// 読まれていない死にフィールドだった。「誰を狙うか」は
// AIBlackboard::targetEntity（知覚システムが毎フレーム解決する）が持つ。
#include "Game/Logics/Character/Boss/Components/BossCommandComponent.h"
#include "Game/Logics/AI/Drone/DroneComponent.h" // kDroneFormationNames
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <> struct ComponentMeta<BossCommandComponent> {
    CCL_META_USING_THIS(BossCommandComponent)

    static constexpr const char *displayName       = "Boss Command (AI Output)";
    static constexpr const char *category          = CCL::Categories::AI;
    static constexpr int         sortPriority      = 160;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // このコンポーネントは「AIが今フレーム何をやってほしいか」の伝票で、
            // 全フィールドが毎フレーム上書きされる実行時の値。保存してはいけない。
            //   実際に Muriel.json には currentActionId=1101(薙ぎ払い) と
            //   requestFormation=5 が焼き付いており、ボスが「薙ぎ払いの最中」として
            //   生まれてきていた。インスペクタでは見たいので、保存だけ止める。
            CFIELD_RT(currentActionId, "現在のActionID", "Current Command")
            CFIELD_ENUM_U8_RT(requestFormation, "フォーメーション要求", kDroneFormationNames.data(), kDroneFormationNames.size(), "Drone Control")
        };
        return fields;
    }
};
REGISTER_COMPONENT(BossCommandComponent, "BossCommandComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossCommandComponent)
