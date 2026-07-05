/// @file  AIComponentsMeta.cpp
/// @brief AI関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Game/Logics/AI/NavAI/AIComponents.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// 1. 脳の状態 (AI State)
// ============================================================================
template <> struct ComponentMeta<AIStateComponent> {
    CCL_META_USING_THIS(AIStateComponent)

    static constexpr const char* displayName    = "AI State / Brain";
    static constexpr const char* category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 150; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AIStateComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Runtime Status");
        const char* stateNames[] = { "Idle", "Patrol", "Investigate", "Chase", "AttackDoor", "AmbushDuct" };
        int stateIdx = static_cast<int>(comp.currentState);
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Current State: %s", stateNames[stateIdx]);
        ImGui::Text("Time in State: %.2f s", comp.timeInState);

        ImGui::Spacing();
        ImGui::SeparatorText("Movement Speed Settings");
        changed |= ImGui::DragFloat("Patrol Speed", &comp.patrolSpeed, 0.05f, 0.0f, 20.0f, "%.2f m/s");
        changed |= ImGui::DragFloat("Investigate Speed", &comp.investigateSpeed, 0.05f, 0.0f, 20.0f, "%.2f m/s");
        changed |= ImGui::DragFloat("Chase Speed", &comp.chaseSpeed, 0.05f, 0.0f, 30.0f, "%.2f m/s");

        ImGui::Spacing();
        ImGui::SeparatorText("Patrol Parameters");
        changed |= ImGui::DragFloat("Patrol Radius", &comp.patrolRadius, 0.5f, 1.0f, 100.0f);
        changed |= ImGui::DragFloat("Wait Time", &comp.patrolWaitTime, 0.1f, 0.0f, 60.0f);

        return changed;
    }
#else
    static bool CustomGui(AIStateComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(patrolSpeed, "Patrol Speed", 0.05f, 0.0f, 20.0f, "Movement")
            CFIELD_FLOAT_RANGED(investigateSpeed, "Investigate Speed", 0.05f, 0.0f, 20.0f, "Movement")
            CFIELD_FLOAT_RANGED(chaseSpeed, "Chase Speed", 0.05f, 0.0f, 30.0f, "Movement")
            CFIELD_FLOAT_RANGED(patrolRadius, "Patrol Radius", 0.5f, 1.0f, 100.0f, "Logic")
            CFIELD_FLOAT_RANGED(patrolWaitTime, "Wait Time", 0.1f, 0.0f, 60.0f, "Logic")
        };
        return fields;
    }
};
REGISTER_COMPONENT(AIStateComponent, "AIState")
REGISTER_COMPONENT_EDITOR_GUI(AIStateComponent)

// ============================================================================
// 2. 知覚能力 (AI Perception) 
// ★修正: BEGIN_FULL を使用しカテゴリと並び順を明記
// ============================================================================
COMPONENT_META_BEGIN_FULL(AIPerceptionComponent, "AIPerception", CCL::Categories::AI, 155, false)
    CFIELD_FLOAT_RANGED(visionRange, "Vision Range (m)", 0.5f, 0.0f, 100.0f, "Vision")
    CFIELD_FLOAT_RANGED(visionAngle, "Vision Angle (deg)", 1.0f, 0.0f, 360.0f, "Vision")
COMPONENT_META_END(AIPerceptionComponent)

// ============================================================================
// 3. 記憶と適応 (AI Memory / Meta AI)
// ============================================================================
template <> struct ComponentMeta<AIMemoryComponent> {
    CCL_META_USING_THIS(AIMemoryComponent)

    static constexpr const char* displayName    = "AI Memory (Meta)";
    static constexpr const char* category       = CCL::Categories::AI; // ★追加
    static constexpr int         sortPriority   = 160; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AIMemoryComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Live Memory");
        ImGui::Text("Anger Level:");
        ImGui::ProgressBar(comp.currentAngerLevel / 100.0f, ImVec2(-1, 0), "");

        ImGui::TextDisabled("Last Known Pos: (%.1f, %.1f, %.1f)",
            comp.lastKnownPos.x, comp.lastKnownPos.y, comp.lastKnownPos.z);

        ImGui::Spacing();
        ImGui::SeparatorText("Adaptation Stats (Learned)");
        changed |= ImGui::InputInt("Tricked By Sound", &comp.trickedBySoundCount);
        changed |= ImGui::InputInt("Duct Usage Observed", &comp.ductUsageObserved);

        return changed;
    }
#else
    static bool CustomGui(AIMemoryComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_INT_RANGED(trickedBySoundCount, "Tricked By Sound", 0, 1000, "Adaptation")
            CFIELD_INT_RANGED(ductUsageObserved, "Duct Usage Observed", 0, 1000, "Adaptation")
        };
        return fields;
    }
};
REGISTER_COMPONENT(AIMemoryComponent, "AIMemory")
REGISTER_COMPONENT_EDITOR_GUI(AIMemoryComponent)