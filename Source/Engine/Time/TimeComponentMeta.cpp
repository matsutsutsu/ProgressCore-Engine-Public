// TimeStateComponent のシリアライズ/インスペクタ定義。

#include "TimeState.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace
{
    const char *const TimeGroupNames[] = {"None", "Player", "Enemy", "Environment", "UI"};
}

template <>
struct ComponentMeta<TimeStateComponent>
{
    CCL_META_USING_THIS(TimeStateComponent)

    static constexpr const char *displayName       = "Time State";
    static constexpr const char *category          = CCL::Categories::Gameplay;
    static constexpr int         sortPriority      = 190;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(TimeStateComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        namespace mi = ComponentMetaImGui;
        bool changed = false;

        ImGui::Text("ローカルDt: %.4f", comp.localDt);
        ImGui::Spacing();

        int currentGroup = static_cast<int>(comp.group);
        if (mi::Combo("時間グループ", &currentGroup, TimeGroupNames, 5))
        {
            comp.group = static_cast<TimeGroup>(currentGroup);
            changed    = true;
        }

        changed |= mi::DragFloat("ヒットストップタイマー", &comp.hitStopTimer, 0.01f, 0.0f, 5.0f);
        changed |= mi::DragFloat("停止倍率", &comp.freezeScale, 0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat("バフ倍率", &comp.buffScale, 0.01f, 0.0f, 5.0f);
        changed |= mi::DragFloat("シネマ倍率", &comp.cinematicScale, 0.01f, 0.0f, 5.0f);

        return changed;
    }
#else
    static bool CustomGui(TimeStateComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(localDt, "ローカルDt", "状態")
                CFIELD_FLOAT_RANGED(hitStopTimer, "ヒットストップタイマー", 0.01f, 0.0f, 5.0f, "状態")
                    CFIELD_FLOAT_RANGED(freezeScale, "停止倍率", 0.01f, 0.0f, 1.0f, "状態")
                        CFIELD_FLOAT_RANGED(buffScale, "バフ倍率", 0.01f, 0.0f, 5.0f, "状態")
                            CFIELD_FLOAT_RANGED(cinematicScale, "シネマ倍率", 0.01f, 0.0f, 5.0f, "状態")
                                CFIELD_ENUM_U8(group, "時間グループ", TimeGroupNames, 5, "状態")};
        return fields;
    }
};

REGISTER_COMPONENT(TimeStateComponent, "TimeState")
REGISTER_COMPONENT_EDITOR_GUI(TimeStateComponent)