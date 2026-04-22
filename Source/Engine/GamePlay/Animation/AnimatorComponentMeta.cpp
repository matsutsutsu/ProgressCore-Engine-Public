#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Animation/Data/AnimStateMachine.h"       // ★追加
#include "Engine/GamePlay/Animation/AnimParametersComponent.h" // ★追加
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Platform/Dialog.h"

// ===================================================================
// 1. AnimatorComponent (肉体・再生状態) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimatorComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Animator";
    static constexpr bool        hasCustomGui = true;

    static bool CustomGui(AnimatorComponent& anim, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        ImGui::DragFloat("Speed", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f);
        if (ImGui::Checkbox("Loop", &anim.isLoop)) changed = true;
        ImGui::Separator();

        // アニメーションの動的プレビュー機能
        ImGui::TextDisabled("Dynamic Preview");
        if (ImGui::Button("Load & Play Sequence... (JSON)", ImVec2(-1, 0))) {
            char filename[256] = {};
            if (Dialog::OpenFileName(filename, 256, "JSON Files\0*.json\0", "Select Anim Sequence", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {

                namespace fs = std::filesystem;
                fs::path absPath = filename;
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);
                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

                const AnimSequence* loadedSeq = ResourceManager::Instance().LoadAnimSequence(finalPath.c_str());

                if (loadedSeq) {
                    anim.Play(loadedSeq, true, 1.0f);
                    changed = true;
                }
            }
        }
        ImGui::Separator();

        if (anim.currentSequence) {
            ImGui::Text("Sequence: %s", anim.currentSequence->sequenceName.c_str());
            ImGui::Text("Target Anim: %s", anim.currentSequence->targetAnimName.c_str());
            ImGui::Text("Time: %.2f / %.2f", anim.currentTimer, anim.currentSequence->duration);
        }
        else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "No Sequence Playing");
        }
        ImGui::Text("Finished: %s", anim.isFinished ? "True" : "False");

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(AnimatorComponent, playbackSpeed, "playbackSpeed", "Playback Speed", 0.05f, -5.0f, 5.0f, "Playback"),
            META_FIELD_BOOL(AnimatorComponent, isLoop, "isLoop", "Loop", "Playback")
        };
        return fields;
    }
};
REGISTER_COMPONENT(AnimatorComponent, "Animator")


// ===================================================================
// 2. AnimStateMachineComponent (ルールと状態) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimStateMachineComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Anim State Machine";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // ランタイム状態なので保存しない

    static bool CustomGui(AnimStateMachineComponent& fsm, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        if (!fsm.graph) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[Error] No AnimGraph Assigned!");
            return false;
        }

        std::string currentStateName = "Unknown";
        for (const auto& state : fsm.graph->states) {
            if (state.stateHash == fsm.currentStateHash) {
                currentStateName = state.sequenceName;
                break;
            }
        }

        ImGui::Text("Current State:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[ %s ]", currentStateName.c_str());
        ImGui::TextDisabled("Hash ID: 0x%X", fsm.currentStateHash);

        ImGui::Separator();
        ImGui::Text("Available States (Click to force transition):");

        ImGui::Indent();
        for (const auto& state : fsm.graph->states) {
            bool isCurrent = (state.stateHash == fsm.currentStateHash);

            if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

            // クリックによる強制遷移テスト機能
            if (ImGui::Selectable(state.sequenceName.c_str(), isCurrent)) {
                fsm.currentStateHash = state.stateHash;
                changed = true;
            }

            if (isCurrent) ImGui::PopStyleColor();
        }
        ImGui::Unindent();

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(AnimStateMachineComponent, "AnimStateMachine")


// ===================================================================
// 3. AnimParametersComponent (掲示板パラメータ) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimParametersComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Anim Parameters";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // 毎フレーム上書きされるため保存しない

    static bool CustomGui(AnimParametersComponent& params, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        ImGui::Text("Float Parameters");
        ImGui::Separator();

        for (auto& p : params.floats) {
            ImGui::PushID(p.hash);
            ImGui::Text("ID: 0x%08X", p.hash);
            ImGui::SameLine(120);
            if (ImGui::DragFloat("##val", &p.value, 0.05f)) {
                changed = true; // マウスで直接書き換えてテスト可能
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Text("Bool Parameters");
        ImGui::Separator();

        for (auto& p : params.bools) {
            ImGui::PushID(p.hash);
            ImGui::Text("ID: 0x%08X", p.hash);
            ImGui::SameLine(120);
            if (ImGui::Checkbox("##val", &p.value)) {
                changed = true;
            }
            ImGui::PopID();
        }

        if (params.floats.empty() && params.bools.empty()) {
            ImGui::TextDisabled("No parameters set.");
        }

        return changed;
    }

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(AnimParametersComponent, "AnimParameters")