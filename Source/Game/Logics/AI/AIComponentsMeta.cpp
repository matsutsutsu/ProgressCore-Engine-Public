#include "Game/Logics/AI/AIComponents.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include <imgui.h>

// ============================================================================
// 1. 脳の状態 (AI State)
// ============================================================================
template <> struct ComponentMeta<AIStateComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "AI State";

    // デバッグ表示とパラメータ調整を同時に行うためCustomGuiを有効化
    static constexpr bool        hasCustomGui = true;

    static bool CustomGui(AIStateComponent& comp, unsigned long long entityID = 0, void* worldPtr = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Runtime Status (Read Only)");

        // 現在のステートを文字列として表示
        const char* stateNames[] = { "Idle", "Patrol", "Investigate", "Chase", "Telekinesis" };
        int stateIdx = static_cast<int>(comp.currentState);

        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Current State: %s", stateNames[stateIdx]);
        ImGui::TextDisabled("Time in State: %.2f sec", comp.timeInState);

        ImGui::Spacing();
        ImGui::SeparatorText("Patrol Settings (Editable)");

        // デザイナーが調整するパラメータ
        changed |= ImGui::DragFloat("Wait Time", &comp.patrolWaitTime, 0.1f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Patrol Radius", &comp.patrolRadius, 0.5f, 1.0f, 100.0f);

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // ステートは保存せず、調整パラメータのみをJSONの保存対象にする
            META_FIELD_FLOAT(AIStateComponent, patrolWaitTime, "patrolWaitTime", "Wait Time", 0.1f, 0.0f, 10.0f, "Settings"),
            META_FIELD_FLOAT(AIStateComponent, patrolRadius, "patrolRadius", "Patrol Radius", 0.5f, 1.0f, 100.0f, "Settings")
        };
        return fields;
    }
};

REGISTER_COMPONENT(AIStateComponent, "AIState")


// ============================================================================
// 2. 知覚能力 (AI Perception)
// ============================================================================
template <> struct ComponentMeta<AIPerceptionComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "AI Perception";

    // 特殊なデバッグ表示が不要なため、メタシステムの自動描画に任せる
    static constexpr bool        hasCustomGui = false;

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(AIPerceptionComponent, visionRange, "visionRange", "Vision Range (m)", 0.5f, 0.0f, 100.0f, "Vision"),
            META_FIELD_FLOAT(AIPerceptionComponent, visionAngle, "visionAngle", "Vision Angle (deg)", 1.0f, 0.0f, 360.0f, "Vision"),
            META_FIELD_FLOAT(AIPerceptionComponent, hearingRadius, "hearingRadius", "Hearing Radius (m)", 0.5f, 0.0f, 100.0f, "Hearing")
        };
        return fields;
    }
};

REGISTER_COMPONENT(AIPerceptionComponent, "AIPerception")


// ============================================================================
// 3. 記憶と適応 (AI Memory / Meta AI)
// ============================================================================
template <> struct ComponentMeta<AIMemoryComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "AI Memory (Meta)";
    static constexpr bool        hasCustomGui = true;

    static bool CustomGui(AIMemoryComponent& comp, unsigned long long entityID = 0, void* worldPtr = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("Live Memory");

        // 怒りレベルをプログレスバーとして視覚化
        ImGui::Text("Anger Level:");
        ImGui::ProgressBar(comp.currentAngerLevel / 100.0f, ImVec2(-1, 0), "");

        // 最後に確認した座標の表示 (lastKnownPlayerPos -> lastKnownPos へ修正)
        ImGui::TextDisabled("Last Known Pos: (%.1f, %.1f, %.1f)",
            comp.lastKnownPos.x, comp.lastKnownPos.y, comp.lastKnownPos.z);

        ImGui::Spacing();
        ImGui::SeparatorText("Adaptation Stats (Learned)");

        // デバッグ目的で適応レベルを手動でいじれるようにしておく
        // ImGui::InputInt にはラベル名と変数のポインタの2つの引数が必要
        changed |= ImGui::InputInt("Tricked By Sound", &comp.trickedBySoundCount);
        changed |= ImGui::InputInt("Duct Usage Observed", &comp.ductUsageObserved);

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            // 新企画の変数名に合わせたJSONセーブデータのマッピング
            META_FIELD_INT(AIMemoryComponent, trickedBySoundCount, "trickedBySoundCount", "Tricked By Sound", 0, 1000, "Adaptation"),
            META_FIELD_INT(AIMemoryComponent, ductUsageObserved, "ductUsageObserved", "Duct Usage Observed", 0, 1000, "Adaptation")
        };
        return fields;
    }
};

REGISTER_COMPONENT(AIMemoryComponent, "AIMemory")