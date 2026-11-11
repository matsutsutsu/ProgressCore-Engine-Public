#include "BossPhaseComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

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
            CFIELD_FLOAT_RANGED(transitionDuration, "移行演出の長さ", 0.05f, 0.5f, 10.0f, "移行演出")
            CFIELD_FLOAT_RANGED(finishSlowScale, "とどめのスロー倍率", 0.01f, 0.01f, 1.0f, "移行演出")
            CFIELD_FLOAT_RANGED(finishSlowDuration, "とどめのスローの長さ", 0.05f, 0.05f, 5.0f, "移行演出")
            CFIELD_FLOAT_RANGED(defeatSlowScale, "撃破のスロー倍率", 0.01f, 0.01f, 1.0f, "撃破演出")
            // 上限は GameResultSystem の kResultDelay より短くする。超えるとムービーの前にリザルトが出る
            CFIELD_FLOAT_RANGED(defeatSlowDuration, "撃破のスローの長さ", 0.05f, 0.05f, 0.95f, "撃破演出")
            CFIELD(tutorialAssetPath, "チュートリアルAIパス", "Tutorial")
            // 形態とタイマーは実行時の値。保存しない
            CFIELD_ENUM_U8_RT(phase, "現在の形態", kBossPhaseNames, kBossPhaseCount, "State")
            CFIELD_RT(timer, "状態滞在タイマー", "State")};
        return fields;
    }
};

REGISTER_COMPONENT(BossPhaseComponent, "BossPhaseComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossPhaseComponent)
