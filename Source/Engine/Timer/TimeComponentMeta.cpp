/// @file  TimeComponentMeta.cpp
/// @brief TimeStateComponent のシリアライズ/インスペクタ定義。

#include "TimeState.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace {
    const char* const TimeGroupNames[] = { "None", "Player", "Enemy", "Environment", "UI" };
}

template <> struct ComponentMeta<TimeStateComponent> {
    CCL_META_USING_THIS(TimeStateComponent)

    static constexpr const char* displayName    = "Time State";
    static constexpr const char* category       = CCL::Categories::Gameplay; // ★追加
    static constexpr int         sortPriority   = 190; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(TimeStateComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::Text("Local Dt: %.4f", comp.localDt);
        ImGui::Spacing();

        int currentGroup = static_cast<int>(comp.group);
        if (ImGui::Combo("Time Group", &currentGroup, TimeGroupNames, 5)) {
            comp.group = static_cast<TimeGroup>(currentGroup);
            changed = true;
        }

        changed |= ImGui::DragFloat("HitStop Timer", &comp.hitStopTimer, 0.01f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat("Freeze Scale", &comp.freezeScale, 0.01f, 0.0f, 1.0f);
        changed |= ImGui::DragFloat("Buff Scale", &comp.buffScale, 0.01f, 0.0f, 5.0f);
        changed |= ImGui::DragFloat("Cinematic Scale", &comp.cinematicScale, 0.01f, 0.0f, 5.0f);

        return changed;
    }
#else
    static bool CustomGui(TimeStateComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(localDt, "Local Dt", "State")
            CFIELD_FLOAT_RANGED(hitStopTimer, "HitStop Timer", 0.01f, 0.0f, 5.0f, "State")
            CFIELD_FLOAT_RANGED(freezeScale, "Freeze Scale", 0.01f, 0.0f, 1.0f, "State")
            CFIELD_FLOAT_RANGED(buffScale, "Buff Scale", 0.01f, 0.0f, 5.0f, "State")
            CFIELD_FLOAT_RANGED(cinematicScale, "Cinematic Scale", 0.01f, 0.0f, 5.0f, "State")
            CFIELD_ENUM_U8(group, "Time Group", TimeGroupNames, 5, "State")
        };
        return fields;
    }
};

REGISTER_COMPONENT(TimeStateComponent, "TimeState")
REGISTER_COMPONENT_EDITOR_GUI(TimeStateComponent)