#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"     
#include "Engine/Animation/Components/AnimParametersComponent.h" 
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include <filesystem>
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===================================================================
// 1. AnimatorComponent (肉体・再生状態) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimatorComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Animator";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimatorComponent& anim, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        ImGui::DragFloat("Speed", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f);
        if (ImGui::Checkbox("Loop", &anim.isLoop)) changed = true;
        ImGui::Separator();

        // アニメーションの動的プレビュー機能
        ImGui::TextDisabled("Dynamic Preview");
        if (ImGui::Button("Load & Play Sequence... (JSON)", ImVec2(-1, 0))) {
            if (auto path = Engine::FileDialog::OpenPreset("AnimSequence", "Select Anim Sequence",
                Graphics::Instance().GetWindowHandle()))
            {
                namespace fs = std::filesystem;
                fs::path absPath = path->string();
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);
                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : path->string();
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
#endif

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
REGISTER_COMPONENT_EDITOR_GUI(AnimatorComponent)

// ===================================================================
// 2. AnimStateMachineComponent (ルールと状態) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimStateMachineComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Anim State Machine";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimStateMachineComponent& fsm, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        // =========================================================
        //  グラフをダイアログからロードするUI
        // =========================================================
        ImGui::TextDisabled("Graph File: %s", fsm.graphPath.empty() ? "None" : fsm.graphPath.c_str());

        if (ImGui::Button("Load Anim Graph... (JSON)", ImVec2(-1, 0))) {
            if (auto path = Engine::FileDialog::OpenPreset("AnimNodes", "Select Anim Nodes",
                Graphics::Instance().GetWindowHandle()))
            {

                namespace fs = std::filesystem;
                fs::path absPath = path->string();
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);
                fsm.graphPath = (!ec && !relPath.empty()) ? relPath.generic_string() : path->string();
                // 即座にロードしてバインドする
                if (AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {})) {
                    // ★修正1: `fsm.internalGraph` を直接操作し、正しい `sequenceFilePath` からロードする
                    for (auto& state : fsm.internalGraph.states) {
                        if (!state.sequenceFilePath.empty()) {
                            state.sequence = ResourceManager::Instance().LoadAnimSequence(state.sequenceFilePath.c_str());
                        }
                    }
                    // ★修正2: 諸悪の根源であった自己ポインタの代入を削除
                    // fsm.graph = &fsm.internalGraph; <- これを消滅させました

                    fsm.currentStateHash = fsm.internalGraph.entryStateHash;
                    changed = true;
                }
            }
        }
        ImGui::Separator();

        // ★修正3: `fsm.graph` へのポインタ依存を完全に断ち切る
        if (fsm.internalGraph.states.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[Error] No AnimGraph Assigned!");
            return false;
        }

        std::string currentStateName = "Unknown";
        for (const auto& state : fsm.internalGraph.states) {
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
        for (const auto& state : fsm.internalGraph.states) {
            bool isCurrent = (state.stateHash == fsm.currentStateHash);

            if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

            if (ImGui::Selectable(state.sequenceName.c_str(), isCurrent)) {
                fsm.currentStateHash = state.stateHash;
                changed = true;
            }

            if (isCurrent) ImGui::PopStyleColor();
        }
        ImGui::Unindent();

        return changed;
    }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_STRING(AnimStateMachineComponent, graphPath, "graphPath", "Graph File Path", "General")
        };
        return fields;
    }
};
REGISTER_COMPONENT(AnimStateMachineComponent, "AnimStateMachine")
REGISTER_COMPONENT_EDITOR_GUI(AnimStateMachineComponent)

// ===================================================================
// 3. AnimParametersComponent (掲示板パラメータ) のインスペクタUI
// ===================================================================
template <> struct ComponentMeta<AnimParametersComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Anim Parameters";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false; // 毎フレーム上書きされるため保存しない

#ifdef ENABLE_EDITOR
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
                changed = true;
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
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {};
        return fields;
    }
};
REGISTER_COMPONENT(AnimParametersComponent, "AnimParameters")
REGISTER_COMPONENT_EDITOR_GUI(AnimParametersComponent)