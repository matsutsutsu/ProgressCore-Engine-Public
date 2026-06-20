/// @file  AILogicComponentsMeta.cpp
/// @brief AI関連の実行時コンポーネント (MoveIntent, Perception, AttackIntent) のメタ定義。
///        純粋な実行時データのため、シリアライズから完全に除外し、インスペクタではReadOnlyとする。

#include "MoveIntentComponent.h"
#include "PerceptionComponent.h"
#include "AttackIntentComponent.h"

// ★ マニュアル絶対ルール：必須ヘッダー
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// 1. MoveIntentComponent
// ===========================================================================
template <> struct ComponentMeta<MoveIntentComponent> {
    static constexpr const char* displayName = "MoveIntent";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // ★ シーン保存から完全に除外

#ifdef ENABLE_EDITOR
    static bool CustomGui(MoveIntentComponent& comp, unsigned long long = 0, void* = nullptr) {
        ImGui::TextDisabled("[Runtime Intent: 毎フレーム上書きされます]");
        ImGui::Separator();

        // プランナーが誤って数値を書き換える事故を防ぐためのフェイルセーフ
        ImGui::BeginDisabled();
        ImGui::Checkbox("Has Target", &comp.hasTarget);
        if (comp.hasTarget) {
            ImGui::DragFloat3("Desired Target", &comp.desiredTarget.x);
            ImGui::DragFloat("Desired Speed", &comp.desiredSpeed);
        }
        else {
            ImGui::TextDisabled("Target: None");
        }
        ImGui::InputInt("Priority", &comp.priority);
        ImGui::EndDisabled();

        return false; // 読み取り専用のため、Undo用の変更フラグは常に false
    }
#else
    // ★ マニュアル絶対ルール：フォールバック
    static bool CustomGui(MoveIntentComponent&, unsigned long long, void*) { return false; }
#endif

    // シリアライズしないため、Fieldsは空を返す
    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(MoveIntentComponent, "AI")
REGISTER_COMPONENT_EDITOR_GUI(MoveIntentComponent)


// ===========================================================================
// 2. PerceptionComponent
// ===========================================================================
template <> struct ComponentMeta<PerceptionComponent> {
    static constexpr const char* displayName = "Perception";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // ★ シーン保存から完全に除外

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
        }
        else {
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

REGISTER_COMPONENT(PerceptionComponent, "AI")
REGISTER_COMPONENT_EDITOR_GUI(PerceptionComponent)


// ===========================================================================
// 3. AttackIntentComponent
// ===========================================================================
template <> struct ComponentMeta<AttackIntentComponent> {
    static constexpr const char* displayName = "AttackIntent";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // ★ シーン保存から完全に除外

#ifdef ENABLE_EDITOR
    static bool CustomGui(AttackIntentComponent& comp, unsigned long long = 0, void* = nullptr) {
        ImGui::TextDisabled("[Runtime Intent: 毎フレーム上書きされます]");
        ImGui::Separator();

        ImGui::BeginDisabled();
        ImGui::Checkbox("Requested", &comp.requested);
        if (comp.requested) {
            ImGui::DragFloat("Damage", &comp.damage);
        }
        else {
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

REGISTER_COMPONENT(AttackIntentComponent, "AI")
REGISTER_COMPONENT_EDITOR_GUI(AttackIntentComponent)