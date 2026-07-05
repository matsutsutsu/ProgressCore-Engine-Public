/// @file  AILogicComponentsMeta.cpp
/// @brief AI関連の実行時コンポーネント (MoveIntent, Perception, AttackIntent) のメタ定義。

#include "Game/Logics/Character/Enemy/MoveIntentComponent.h"
#include "Game/Logics/Character/Enemy/PerceptionComponent.h"
#include "Game/Logics/Character/Enemy/AttackIntentComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h" 
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson {
    template <> struct ComponentSerializer<MoveIntentComponent> {
        static void Save(json&, const MoveIntentComponent&, SaveContext*) {}
        static void Load(const json&, MoveIntentComponent&, LoadContext*) {}
    };
    template <> struct ComponentSerializer<PerceptionComponent> {
        static void Save(json&, const PerceptionComponent&, SaveContext*) {}
        static void Load(const json&, PerceptionComponent&, LoadContext*) {}
    };
    template <> struct ComponentSerializer<AttackIntentComponent> {
        static void Save(json&, const AttackIntentComponent&, SaveContext*) {}
        static void Load(const json&, AttackIntentComponent&, LoadContext*) {}
    };
} 

// ===========================================================================
// ★ 実行時データではあるが AI カテゴリとして下層に明示的に配置
// ===========================================================================

// --- 1. MoveIntentComponent ---
template <> struct ComponentMeta<MoveIntentComponent> {
    CCL_META_USING_THIS(MoveIntentComponent)

    static constexpr const char* displayName    = "MoveIntent";
    static constexpr const char* category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 190; // ★追加
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true; 

#ifdef ENABLE_EDITOR
    static bool CustomGui(MoveIntentComponent& comp, unsigned long long = 0, void* = nullptr) {
        ImGui::TextDisabled("[Runtime Intent: 毎フレーム上書きされます]");
        ImGui::Separator();
        ImGui::BeginDisabled();
        ImGui::Checkbox("Has Target", &comp.hasTarget);
        if (comp.hasTarget) {
            ImGui::DragFloat3("Desired Target", &comp.desiredTarget.x);
            ImGui::DragFloat("Desired Speed", &comp.desiredSpeed);
        } else {
            ImGui::TextDisabled("Target: None");
        }
        ImGui::InputInt("Priority", &comp.priority);
        ImGui::EndDisabled();
        return false;
    }
#else
    static bool CustomGui(MoveIntentComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(MoveIntentComponent, "MoveIntentComponent")
REGISTER_COMPONENT_EDITOR_GUI(MoveIntentComponent)

// --- 2. PerceptionComponent ---
template <> struct ComponentMeta<PerceptionComponent> {
    CCL_META_USING_THIS(PerceptionComponent)

    static constexpr const char* displayName    = "Perception";
    static constexpr const char* category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 191; // ★追加
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(PerceptionComponent& comp, unsigned long long = 0, void* = nullptr) {
        ImGui::TextDisabled("[Runtime Sensor: EnemyPerceptionSystem出力]");
        ImGui::Separator();
        ImGui::BeginDisabled();
        if (comp.target != CCL::ECS::InvalidEntityID) {
            ImGui::Text("Target Entity : %llu", static_cast<unsigned long long>(comp.target));
            ImGui::Text("Distance      : %.2f m", comp.distance);
            ImGui::DragFloat3("Last Known Pos", &comp.lastKnownPos.x);
            ImGui::DragFloat3("Dir To Target", &comp.dirToTarget.x);
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No Target Detected");
        }
        ImGui::EndDisabled();
        return false;
    }
#else
    static bool CustomGui(PerceptionComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(PerceptionComponent, "PerceptionComponent")
REGISTER_COMPONENT_EDITOR_GUI(PerceptionComponent)

// --- 3. AttackIntentComponent ---
template <> struct ComponentMeta<AttackIntentComponent> {
    CCL_META_USING_THIS(AttackIntentComponent)

    static constexpr const char* displayName    = "AttackIntent";
    static constexpr const char* category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 192; // ★追加
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AttackIntentComponent& comp, unsigned long long = 0, void* = nullptr) {
        ImGui::TextDisabled("[Runtime Intent: 毎フレーム上書きされます]");
        ImGui::Separator();
        ImGui::BeginDisabled();
        ImGui::Checkbox("Requested", &comp.requested);
        if (comp.requested) {
            ImGui::DragFloat("Damage", &comp.damage);
        } else {
            ImGui::TextDisabled("Damage: %.1f", comp.damage);
        }
        ImGui::EndDisabled();
        return false;
    }
#else
    static bool CustomGui(AttackIntentComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(AttackIntentComponent, "AttackIntentComponent")
REGISTER_COMPONENT_EDITOR_GUI(AttackIntentComponent)