// BossPhaseComponent のシリアライズ/インスペクタ定義。
#include "BossPhaseComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// 表示名は BossPhaseComponent.h の kBossPhaseNames と共有する（二重管理を避ける）

template <>
struct ComponentMeta<BossPhaseComponent>
{
    CCL_META_USING_THIS(BossPhaseComponent)

    static constexpr const char *displayName       = "Boss Phase (形態)";
    static constexpr const char *category          = CCL::Categories::Combat;
    static constexpr int         sortPriority      = 175;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(phase2AssetPath, "第2形態AIパス", "Phase")
            CFIELD_FLOAT_RANGED(phase2HealthThreshold, "第2形態移行HP閾値", 0.1f, 0.0f, 100.0f, "Phase")
            CFIELD(tutorialAssetPath, "チュートリアルAIパス", "Tutorial")
            // 形態とタイマーは実行時の値。保存しない。
            //   Dead や Transition のまま保存すると、次の個体が
            //   「死んだ状態」「形態移行の演出中」で生まれてくる。
            CFIELD_ENUM_U8_RT(phase, "現在の形態", kBossPhaseNames, kBossPhaseCount, "State")
            CFIELD_RT(timer, "状態滞在タイマー", "State")};
        return fields;
    }
};

REGISTER_COMPONENT(BossPhaseComponent, "BossPhaseComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossPhaseComponent)
