/// @file  WandRuntimeComponentsMeta.cpp
/// @brief WandRuntimeComponent / WandFireIntentComponent のメタ定義。
///        FireIntent はデバッグ用に「手動発射トグル」を持たせ、入力System無しでもテスト可能にする。

#include "Game/Logics/Wand/Bullet/WandRuntimeComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// WandRuntimeComponent (パターンA：全自動生成)
// ===========================================================================
COMPONENT_META_BEGIN(WandRuntimeComponent, "Wand Runtime")
CFIELD(cooldownTimer, "Cooldown Timer", "Runtime")
COMPONENT_META_END(WandRuntimeComponent)

// ===========================================================================
// WandFireIntentComponent (パターンB：カスタムGUI / セーブ不要)
// ===========================================================================
template <> struct ComponentMeta<WandFireIntentComponent> {
    CCL_META_USING_THIS(WandFireIntentComponent)
        static constexpr const char* displayName = "Wand Fire Intent";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // 実行時入力。保存しない。

#ifdef ENABLE_EDITOR
    static bool CustomGui(WandFireIntentComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        if (ImGui::Checkbox("Want Fire (debug)", &comp.wantFire)) changed = true;

        float aim[3] = { comp.aimDirection.x, comp.aimDirection.y, comp.aimDirection.z };
        if (ImGui::DragFloat3("Aim Direction", aim, 0.01f)) {
            comp.aimDirection = { aim[0], aim[1], aim[2] };
            changed = true;
        }

        float mz[3] = { comp.muzzlePosition.x, comp.muzzlePosition.y, comp.muzzlePosition.z };
        if (ImGui::DragFloat3("Muzzle Position", mz, 0.1f)) {
            comp.muzzlePosition = { mz[0], mz[1], mz[2] };
            changed = true;
        }

        return changed;
    }
#else
    static bool CustomGui(WandFireIntentComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(WandFireIntentComponent, "Wand Fire Intent")
REGISTER_COMPONENT_EDITOR_GUI(WandFireIntentComponent)