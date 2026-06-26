/// @file  AnimatorComponentMeta.cpp
/// @brief アニメーション関連コンポーネント (Animator, AnimStateMachine, AnimParameters) のシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"     
#include "Engine/Animation/Components/AnimParametersComponent.h" 
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include <filesystem>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===================================================================
// 1. AnimatorComponent (パターンB)
// ===================================================================
template <> struct ComponentMeta<AnimatorComponent> {
    CCL_META_USING_THIS(AnimatorComponent)
        static constexpr const char* displayName = "Animator";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimatorComponent& anim, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;

        if (ImGui::DragFloat("Speed", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f)) changed = true;
        if (ImGui::Checkbox("Loop", &anim.isLoop)) changed = true;
        ImGui::Separator();

        ImGui::TextDisabled("Dynamic Preview");
        if (ImGui::Button("Load & Play Sequence... (JSON)", ImVec2(-1, 0))) {
            if (auto path = Engine::FileDialog::OpenPreset("AnimSequence", "Select Anim Sequence", Graphics::Instance().GetWindowHandle())) {
                namespace fs = std::filesystem;
                fs::path absPath = path->string();
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);
                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : path->string();
                const AnimSequence* loadedSeq = ResourceManager::Instance().LoadAnimSequence(finalPath.c_str());

                if (loadedSeq) {
                    anim.Play(loadedSeq, { .blendTime = 0.2f, .loop = true, .speed = 1.0f });
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
#else
    static bool CustomGui(AnimatorComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(playbackSpeed, "Playback Speed", 0.05f, -5.0f, 5.0f, "Playback")
            CFIELD(isLoop, "Loop", "Playback")
        };
        return fields;
    }
};
REGISTER_COMPONENT(AnimatorComponent, "Animator")
REGISTER_COMPONENT_EDITOR_GUI(AnimatorComponent)

// ===================================================================
// 2. AnimStateMachineComponent (パターンB)
// ===================================================================
template <>
struct ComponentMeta<AnimStateMachineComponent>
{
    CCL_META_USING_THIS(AnimStateMachineComponent)
    static constexpr const char *displayName    = "Anim State Machine";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimStateMachineComponent &fsm, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::TextDisabled("Graph File: %s", fsm.graphPath.empty() ? "None" : fsm.graphPath.c_str());

        // -------------------------------------------------------------
        // 【修正点】グラフのロード処理をラムダ式として共通化 (DRY原則)
        // -------------------------------------------------------------
        auto reloadGraph = [&]() {
            if (AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {}))
            {
                // ロード成功後、シーケンスのポインタを再紐付けする
                for (auto &state : fsm.internalGraph.states)
                {
                    if (!state.sequenceFilePath.empty())
                    {
                        state.sequence = ResourceManager::Instance().LoadAnimSequence(state.sequenceFilePath.c_str());
                    }
                }
                // エントリーステートに戻す
                fsm.currentStateHash = fsm.internalGraph.entryStateHash;
                changed              = true;
            }
        };

        // -------------------------------------------------------------
        // UIレイアウト: 新規ロードボタンと更新ボタンを配置
        // -------------------------------------------------------------
        if (ImGui::Button("Load Anim Graph... (JSON)"))
        {
            if (auto path = Engine::FileDialog::OpenPreset("AnimNodes", "Select Anim Nodes", Graphics::Instance().GetWindowHandle()))
            {
                namespace fs                = std::filesystem;
                fs::path        absPath     = path->string();
                fs::path        currentPath = fs::current_path();
                std::error_code ec;
                fs::path        relPath = fs::relative(absPath, currentPath, ec);

                // 相対パスに変換して保存
                fsm.graphPath = (!ec && !relPath.empty()) ? relPath.generic_string() : path->string();

                // 共通のロード処理を実行
                reloadGraph();
            }
        }

        // パスが既に設定されている場合のみ、横にReloadボタンを表示
        if (!fsm.graphPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Reload Graph"))
            {
                reloadGraph();
            }
        }
        ImGui::Separator();

        if (fsm.internalGraph.states.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[Error] No AnimGraph Assigned!");
            return false;
        }

        std::string currentStateName = "Unknown";
        for (const auto &state : fsm.internalGraph.states)
        {
            if (state.stateHash == fsm.currentStateHash)
            {
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
        for (const auto &state : fsm.internalGraph.states)
        {
            bool isCurrent = (state.stateHash == fsm.currentStateHash);

            if (isCurrent)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

            if (ImGui::Selectable(state.sequenceName.c_str(), isCurrent))
            {
                fsm.currentStateHash = state.stateHash;
                changed              = true;
            }

            if (isCurrent)
                ImGui::PopStyleColor();
        }
        ImGui::Unindent();

        return changed;
    }
#else
    static bool CustomGui(AnimStateMachineComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(graphPath, "Graph File Path", "General")};
        return fields;
    }
};
REGISTER_COMPONENT(AnimStateMachineComponent, "AnimStateMachine")
REGISTER_COMPONENT_EDITOR_GUI(AnimStateMachineComponent)

// ===================================================================
// 3. AnimParametersComponent (パターンB / 非保存)
// ===================================================================
template <> struct ComponentMeta<AnimParametersComponent> {
    static constexpr const char* displayName = "Anim Parameters";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = false;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimParametersComponent& params, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
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
#else
    static bool CustomGui(AnimParametersComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(AnimParametersComponent, "AnimParameters")
REGISTER_COMPONENT_EDITOR_GUI(AnimParametersComponent)