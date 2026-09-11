// アニメーション関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include "Engine/Animation/Core/AnimGraphSerializer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Platform/Core/FileSystem.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===================================================================
// 1. AnimatorComponent
// ===================================================================
template <>
struct ComponentMeta<AnimatorComponent>
{
    CCL_META_USING_THIS(AnimatorComponent)

    static constexpr const char *displayName       = "Animator";
    static constexpr const char *category          = CCL::Categories::Animation;
    static constexpr int         sortPriority      = 100;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimatorComponent &anim, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        if (ImGui::DragFloat("再生速度", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f)) changed = true;
        if (ImGui::Checkbox("ループ", &anim.isLoop)) changed = true;
        ImGui::Separator();

        ImGui::TextDisabled("動的プレビュー");
        if (ImGui::Button("シーケンスを読込&再生... (JSON)", ImVec2(-1, 0)))
        {
            if (auto pathOpt = FileDialog::OpenPreset("AnimSequence"))
            {
                std::string         finalPath = FileDialog::ConvertToRelativePath(*pathOpt);
                const AnimSequence *loadedSeq = ResourceManager::Instance().LoadAnimSequence(finalPath.c_str());

                if (loadedSeq)
                {
                    anim.Play(loadedSeq, {.blendTime = 0.2f, .loop = true, .speed = 1.0f});
                    changed = true;
                }
            }
        }
        ImGui::Separator();

        if (anim.currentSequence)
        {
            ImGui::Text("シーケンス: %s", anim.currentSequence->sequenceName.c_str());
            ImGui::Text("対象アニメ: %s", anim.currentSequence->targetAnimName.c_str());
            ImGui::Text("時間: %.2f / %.2f", anim.currentTimer, anim.currentSequence->duration);
        }
        else
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "再生中のシーケンスなし");
        }
        ImGui::Text("終了: %s", anim.isFinished ? "True" : "False");

        return changed;
    }
#else
    static bool CustomGui(AnimatorComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(playbackSpeed, "再生速度", 0.05f, -5.0f, 5.0f, "再生")
                CFIELD(isLoop, "ループ", "再生")};
        return fields;
    }
};
REGISTER_COMPONENT(AnimatorComponent, "Animator")
REGISTER_COMPONENT_EDITOR_GUI(AnimatorComponent)

// ===================================================================
// 2. AnimStateMachineComponent
// ===================================================================
template <>
struct ComponentMeta<AnimStateMachineComponent>
{
    CCL_META_USING_THIS(AnimStateMachineComponent)

    static constexpr const char *displayName       = "Anim State Machine";
    static constexpr const char *category          = CCL::Categories::Animation;
    static constexpr int         sortPriority      = 105;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimStateMachineComponent &fsm, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::TextDisabled("グラフファイル: %s", fsm.graphPath.empty() ? "なし" : fsm.graphPath.c_str());

        auto reloadGraph = [&]() {
            if (AnimGraphSerializer::LoadFromJSON(fsm.internalGraph, fsm.graphPath, {}))
            {
                for (auto &state : fsm.internalGraph.states)
                {
                    if (!state.sequenceFilePath.empty())
                    {
                        state.sequence = ResourceManager::Instance().LoadAnimSequence(state.sequenceFilePath.c_str());
                    }
                }
                fsm.currentStateHash = fsm.internalGraph.entryStateHash;
                changed              = true;
            }
        };

        if (ImGui::Button("アニメグラフを読込... (JSON)"))
        {
            if (auto pathOpt = FileDialog::OpenPreset("AnimNodes"))
            {
                fsm.graphPath = FileDialog::ConvertToRelativePath(*pathOpt);
                reloadGraph();
                changed = true;
            }
        }

        if (!fsm.graphPath.empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("グラフ再読込"))
            {
                reloadGraph();
            }
        }
        ImGui::Separator();

        if (fsm.internalGraph.states.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[エラー] アニメグラフが未設定です!");
            return changed;
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

        ImGui::Text("現在の状態:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[ %s ]", currentStateName.c_str());
        ImGui::TextDisabled("Hash ID: 0x%X", fsm.currentStateHash);

        ImGui::Separator();
        ImGui::Text("利用可能な状態:");

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
            CFIELD(graphPath, "グラフファイルパス", "全般")};
        return fields;
    }
};
REGISTER_COMPONENT(AnimStateMachineComponent, "AnimStateMachine")
REGISTER_COMPONENT_EDITOR_GUI(AnimStateMachineComponent)

// ===================================================================
// 3. AnimParametersComponent
// ===================================================================
template <>
struct ComponentMeta<AnimParametersComponent>
{
    static constexpr const char *displayName       = "Anim Parameters";
    static constexpr const char *category          = CCL::Categories::Animation;
    static constexpr int         sortPriority      = 120;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AnimParametersComponent &params, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::Text("Floatパラメータ");
        ImGui::Separator();

        for (auto &p : params.floats)
        {
            ImGui::PushID(p.hash);
            ImGui::Text("ID: 0x%08X", p.hash);
            ImGui::SameLine(120);
            if (ImGui::DragFloat("##val", &p.value, 0.05f))
            {
                changed = true;
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Text("Boolパラメータ");
        ImGui::Separator();

        for (auto &p : params.bools)
        {
            ImGui::PushID(p.hash);
            ImGui::Text("ID: 0x%08X", p.hash);
            ImGui::SameLine(120);
            if (ImGui::Checkbox("##val", &p.value))
            {
                changed = true;
            }
            ImGui::PopID();
        }

        if (params.floats.empty() && params.bools.empty())
        {
            ImGui::TextDisabled("パラメータ未設定");
        }

        return changed;
    }
#else
    static bool CustomGui(AnimParametersComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(AnimParametersComponent, "AnimParameters")
REGISTER_COMPONENT_EDITOR_GUI(AnimParametersComponent)