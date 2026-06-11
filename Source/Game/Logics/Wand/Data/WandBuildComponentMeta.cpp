/// @file  WandBuildComponentMeta.cpp
/// @brief WandBuildComponent のメタ定義。
///        フェーズ×スロットを編集できる専用GUIを提供する。
///        変更時に isDirty=true を立て、次フレームの WandCompileSystem で再コンパイルさせる。

#include "Game/Logics/Wand/Data/WandBuildComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// GUI用の文字列配列のみを無名名前空間に閉じ込める
namespace {
#ifdef ENABLE_EDITOR
    const char* const kEffectNames[] = {
        "None", "Accelerate", "Slow", "Focus", "Homing",
        "Explosion", "Split", "Reflection", "Chaos"
    };
    const char* const kPhaseNames[] = { "Spawn", "Travel", "OnHit", "Inheritance" };
#endif
} // namespace

template <> struct ComponentMeta<WandBuildComponent> {
    CCL_META_USING_THIS(WandBuildComponent)
        static constexpr const char* displayName = "Wand Build";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(WandBuildComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        changed |= ImGui::DragFloat("Base Speed", &comp.baseSpeed, 0.1f, 0.0f, 1000.0f);
        changed |= ImGui::DragFloat("Base Lifetime", &comp.baseLifetime, 0.05f, 0.0f, 60.0f);
        changed |= ImGui::DragFloat("Cooldown", &comp.cooldown, 0.01f, 0.0f, 10.0f);
        ImGui::Separator();

        for (int p = 0; p < static_cast<int>(WandPhase::Count); ++p) {
            ImGui::PushID(p);
            ImGui::TextUnformatted(kPhaseNames[p]);

            int count = comp.slotCounts[p];
            if (ImGui::SliderInt("Slots", &count, 0, static_cast<int>(MAX_SLOTS_PER_PHASE))) {
                comp.slotCounts[p] = static_cast<uint8_t>(count);
                changed = true;
            }

            for (int s = 0; s < comp.slotCounts[p]; ++s) {
                ImGui::PushID(s);
                int cur = static_cast<int>(comp.slots[p][s]);
                if (ImGui::Combo("##slot", &cur, kEffectNames, 9)) {
                    comp.slots[p][s] = static_cast<EffectType>(cur);
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
            ImGui::Separator();
        }

        if (comp.cachedProgramId == InvalidProgramID) {
            ImGui::TextDisabled("ProgramID: (uncompiled)");
        }
        else {
            ImGui::Text("ProgramID: %u", comp.cachedProgramId);
        }

        // 何か変えたら再コンパイルを要求する。
        if (changed) comp.isDirty = true;

        return changed;
    }
#else
    static bool CustomGui(WandBuildComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(baseSpeed,    "Base Speed",    "Wand")
            CFIELD(baseLifetime, "Base Lifetime", "Wand")
            CFIELD(cooldown,     "Cooldown",      "Wand")
        };
        return fields;
    }
};

REGISTER_COMPONENT(WandBuildComponent, "Wand Build")
REGISTER_COMPONENT_EDITOR_GUI(WandBuildComponent)