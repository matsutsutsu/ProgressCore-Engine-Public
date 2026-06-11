// ===========================================================================
// Data/WandRuntimeComponents.cpp
// ------------------------------
// @file
// @brief WandRuntimeComponent / WandFireIntentComponent のMeta。
//        FireIntent はデバッグ用に「手動発射トグル」を持たせ、入力System無しでもテスト可能にする。
// ===========================================================================
#include "Game/Logics/Wand/Data/WandRuntimeComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#endif

// --- WandRuntimeComponent（シンプル：自動生成） ---
COMPONENT_META_BEGIN(WandRuntimeComponent, "Wand Runtime")
CFIELD(cooldownTimer, "Cooldown Timer", "Runtime")
COMPONENT_META_END(WandRuntimeComponent)

// --- WandFireIntentComponent（カスタムGUI：手動発射トグル） ---
namespace
{
    template <> struct ComponentMeta<WandFireIntentComponent>
    {
        CCL_META_USING_THIS(WandFireIntentComponent)
            static constexpr const char* displayName = "Wand Fire Intent";
        static constexpr bool        hasCustomGui = true;
        static constexpr bool        isSerializable = false; // 実行時入力。保存しない。

        static bool CustomGui(WandFireIntentComponent& comp,
            unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr)
        {
            bool changed = false;
#ifdef ENABLE_EDITOR
            if (ImGui::Checkbox("Want Fire (debug)", &comp.wantFire)) changed = true;

            float aim[3] = { comp.aimDirection.x, comp.aimDirection.y, comp.aimDirection.z };
            if (ImGui::DragFloat3("Aim Direction", aim, 0.01f))
            {
                comp.aimDirection = { aim[0], aim[1], aim[2] };
                changed = true;
            }

            float mz[3] = { comp.muzzlePosition.x, comp.muzzlePosition.y, comp.muzzlePosition.z };
            if (ImGui::DragFloat3("Muzzle Position", mz, 0.1f))
            {
                comp.muzzlePosition = { mz[0], mz[1], mz[2] };
                changed = true;
            }
#endif
            return changed;
        }

        static const std::vector<FieldDescriptor>& Fields()
        {
            static const std::vector<FieldDescriptor> fields = {};
            return fields;
        }
    };
} // namespace

REGISTER_COMPONENT(WandFireIntentComponent, "Wand Fire Intent")
REGISTER_COMPONENT_EDITOR_GUI(WandFireIntentComponent)