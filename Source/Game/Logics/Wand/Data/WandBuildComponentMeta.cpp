// ===========================================================================
// Data/WandBuildComponent.cpp
// ---------------------------
// @file
// @brief WandBuildComponent のMeta。フェーズ×スロットを編集できる専用GUIを提供する。
//        変更時に isDirty=true を立て、次フレームの WandCompileSystem で再コンパイルさせる。
// @note slots(2次元enum配列)は自動シリアライズ対象外（実行時編集専用）。スカラのみ保存される。
//       save/load での slots 永続化は後工程で対応。
// ===========================================================================
#include "Game/Logics/Wand/Data/WandBuildComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

namespace
{
#ifdef ENABLE_EDITOR
    const char* kEffectNames[] = {
        "None", "Accelerate", "Slow", "Focus", "Homing",
        "Explosion", "Split", "Reflection", "Chaos"
    };
    const char* kPhaseNames[] = { "Spawn", "Travel", "OnHit", "Inheritance" };
#endif

    template <> struct ComponentMeta<WandBuildComponent>
    {
        CCL_META_USING_THIS(WandBuildComponent)
            static constexpr const char* displayName = "Wand Build";
        static constexpr bool        hasCustomGui = true;
        static constexpr bool        isSerializable = true;

        static bool CustomGui(WandBuildComponent& comp,
            unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
        {
            bool changed = false;
#ifdef ENABLE_EDITOR
            if (ImGui::DragFloat("Base Speed", &comp.baseSpeed, 0.1f, 0.0f, 1000.0f)) changed = true;
            if (ImGui::DragFloat("Base Lifetime", &comp.baseLifetime, 0.05f, 0.0f, 60.0f))   changed = true;
            if (ImGui::DragFloat("Cooldown", &comp.cooldown, 0.01f, 0.0f, 10.0f))   changed = true;
            ImGui::Separator();

            for (int p = 0; p < static_cast<int>(WandPhase::Count); ++p)
            {
                ImGui::PushID(p);
                ImGui::TextUnformatted(kPhaseNames[p]);

                int count = comp.slotCounts[p];
                if (ImGui::SliderInt("Slots", &count, 0, static_cast<int>(MAX_SLOTS_PER_PHASE)))
                {
                    comp.slotCounts[p] = static_cast<uint8_t>(count);
                    changed = true;
                }

                for (int s = 0; s < comp.slotCounts[p]; ++s)
                {
                    ImGui::PushID(s);
                    int cur = static_cast<int>(comp.slots[p][s]);
                    if (ImGui::Combo("##slot", &cur, kEffectNames, IM_ARRAYSIZE(kEffectNames)))
                    {
                        comp.slots[p][s] = static_cast<EffectType>(cur);
                        changed = true;
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
                ImGui::Separator();
            }

            if (comp.cachedProgramId == InvalidProgramID)
                ImGui::TextDisabled("ProgramID: (uncompiled)");
            else
                ImGui::Text("ProgramID: %u", comp.cachedProgramId);

            // 何か変えたら再コンパイルを要求する。
            if (changed) comp.isDirty = true;
#endif
            return changed;
        }

        static const std::vector<FieldDescriptor>& Fields()
        {
            static const std::vector<FieldDescriptor> fields = {
                CFIELD(baseSpeed,    "Base Speed",    "Wand")
                CFIELD(baseLifetime, "Base Lifetime", "Wand")
                CFIELD(cooldown,     "Cooldown",      "Wand")
            };
            return fields;
        }
    };
} // namespace

REGISTER_COMPONENT(WandBuildComponent, "Wand Build")
REGISTER_COMPONENT_EDITOR_GUI(WandBuildComponent)