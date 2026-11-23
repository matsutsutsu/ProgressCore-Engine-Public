#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Animation/Components/AnimParametersComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include "Engine/Animation/Systems/AnimControllerSystem.h"
#include <algorithm>
#include "Engine/Platform/Core/Dialog.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ===========================================================================
// AnimatorComponent
// ===========================================================================
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
    // 再生の中身はアニメグラフが決めるので、ここは今の再生の確認だけにする
    static bool CustomGui(AnimatorComponent &anim, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        if (!anim.currentSequence)
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "再生中のシーケンスなし");
            return false;
        }

        const AnimSequence *seq = anim.currentSequence;
        ImGui::Text("%s", seq->sequenceName.c_str());
        ImGui::TextDisabled("%.2f / %.2f 秒%s", anim.currentTimer, seq->duration, anim.isFinished ? "（終了）" : "");

        // ブレンドスペースの再生中は、点ごとの重みを出せる（どのクリップがどれだけ混ざっているか）
        //   点が多いと縦に伸びるので、既定では閉じておく
        const AnimBlendPlayback &blend = anim.currentBlend;
        if (blend.IsActive() &&
            ImGui::TreeNode("##BlendWeights", "ブレンドの重み（入力 %.2f, %.2f）", blend.inputX, blend.inputY))
        {
            for (int i = 0; i < blend.sampleCount; ++i)
            {
                const char *name = blend.sequences[i] ? blend.sequences[i]->sequenceName.c_str() : "(未設定)";
                ImGui::ProgressBar(blend.weights[i], ImVec2(80.0f, 0.0f));
                ImGui::SameLine();
                ImGui::TextUnformatted(name);
            }
            ImGui::TreePop();
        }

        return false;
    }
#else
    static bool CustomGui(AnimatorComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    // 保存する値はない（再生速度・ループはステートの設定で、再生のたびに Play が書く）
    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};
REGISTER_COMPONENT(AnimatorComponent, "Animator")
REGISTER_COMPONENT_EDITOR_GUI(AnimatorComponent)

// ===========================================================================
// AnimStateMachineComponent
// ===========================================================================
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
    static bool CustomGui(AnimStateMachineComponent &fsm, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool changed = false;

        // 再生とパラメータの初期値は、同じ Entity の別のコンポーネントへ入れる
        auto                    *world    = static_cast<CCL::ECS::World *>(worldPtr);
        const auto               id       = static_cast<CCL::ECS::EntityID>(entityID);
        AnimatorComponent       *animator = world ? world->GetComponent<AnimatorComponent>(id) : nullptr;
        AnimParametersComponent *params   = world ? world->GetComponent<AnimParametersComponent>(id) : nullptr;

        ImGui::TextDisabled("グラフファイル: %s", fsm.graphPath.empty() ? "なし" : fsm.graphPath.c_str());

        // 読込は AnimControllerSystem の初回読込と同じ関数を通す（entry への入り方をそろえるため）
        //   エディタの一時停止中は Logic が回らないので、要求を積まずにその場で読む
        if (ImGui::Button("アニメグラフを読込... (JSON)"))
        {
            if (auto pathOpt = FileDialog::OpenPreset("AnimNodes"))
            {
                const std::string oldPath = fsm.graphPath;
                fsm.graphPath             = FileDialog::ConvertToRelativePath(*pathOpt);
                if (AnimControllerSystem::LoadGraph(fsm, animator, params))
                {
                    changed = true;
                }
                else
                {
                    // 読めなければ、グラフもパスも元のまま
                    fsm.graphPath = oldPath;
                }
            }
        }

        if (!fsm.graphPath.empty())
        {
            ImGui::SameLine();
            // ファイルを読み直す。同じグラフを使うほかの Entity にも反映される
            if (ImGui::Button("グラフ再読込"))
            {
                if (AnimControllerSystem::ReloadGraph(fsm, animator, params)) changed = true;
            }
        }
        ImGui::Separator();

        if (!fsm.graph || fsm.graph->states.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[エラー] アニメグラフが未設定です!");
            return changed;
        }

        // サブマシンの中にいるときは、外側からの道筋で出す（例: Atk_Combo > Atk_Combo1）
        ImGui::Text("現在:");
        ImGui::SameLine();
        ImGui::TextColored(kActiveColor, "%s", BuildActivePathLabel(fsm).c_str());

        // ステートが多く、開いたままだとインスペクタが縦に伸びるので、既定では閉じておく
        if (ImGui::TreeNode("##StateJump", "ステートへジャンプ（%d）", CountStates(*fsm.graph)))
        {
            // 名前で絞ると、階層をたどらずに目的のステートを探せる
            static ImGuiTextFilter filter;
            filter.Draw("検索", -FLT_MIN);

            if (filter.IsActive())
            {
                if (DrawMatchingStates(fsm, animator, *fsm.graph, filter, "")) changed = true;
            }
            else
            {
                if (DrawStateTree(fsm, animator, *fsm.graph)) changed = true;
            }
            ImGui::TreePop();
        }

        return changed;
    }

    static constexpr ImVec4 kActiveColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f); // 今いるステートの色

    // 今いるステートの名前を、ルートから葉まで " > " でつないで返す
    static std::string BuildActivePathLabel(const AnimStateMachineComponent &fsm)
    {
        std::string           label;
        const AnimStateGraph *graph = fsm.graph;
        for (uint32_t hash : fsm.activePath)
        {
            if (!graph) break;

            const AnimState *state = FindDirectState(*graph, hash);
            if (!state) break;

            if (!label.empty()) label += " > ";
            label += state->stateName;
            graph = state->subGraph.get();
        }
        return label.empty() ? "Unknown" : label;
    }

    // グラフの中のステートの数（サブマシンの中も数える）
    static int CountStates(const AnimStateGraph &graph)
    {
        int count = 0;
        for (const auto &state : graph.states)
        {
            ++count;
            if (state.isSubMachine && state.subGraph) count += CountStates(*state.subGraph);
        }
        return count;
    }

    // 葉だけでなく、葉を含むサブマシンも今いる所として扱う
    static bool IsOnActivePath(const AnimStateMachineComponent &fsm, uint32_t hash)
    {
        return std::find(fsm.activePath.begin(), fsm.activePath.end(), hash) != fsm.activePath.end();
    }

    // ステートを階層の木で並べ、クリックしたステートへジャンプする。ジャンプしたら true
    // サブマシンは矢印で開閉し、名前のクリックでジャンプする。今いるサブマシンは開いた状態で出す
    static bool DrawStateTree(AnimStateMachineComponent &fsm, AnimatorComponent *animator, const AnimStateGraph &graph)
    {
        bool jumped = false;
        for (const auto &state : graph.states)
        {
            const bool isActive = IsOnActivePath(fsm, state.stateHash);
            const bool isSub    = state.isSubMachine && state.subGraph;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (isSub) flags |= ImGuiTreeNodeFlags_OpenOnArrow;
            else flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (isActive) flags |= ImGuiTreeNodeFlags_Selected;

            ImGui::PushID(static_cast<int>(state.stateHash));
            if (isActive) ImGui::PushStyleColor(ImGuiCol_Text, kActiveColor);
            if (isSub && isActive) ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);

            const bool open    = ImGui::TreeNodeEx("##State", flags, "%s%s", isSub ? "[Sub] " : "", state.stateName.c_str());
            const bool clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
            if (isActive) ImGui::PopStyleColor();

            // Animator がないとクリップを流せないので、ジャンプさせない
            if (clicked && animator && AnimControllerSystem::JumpToState(fsm, *animator, state.stateHash)) jumped = true;

            if (isSub && open)
            {
                if (DrawStateTree(fsm, animator, *state.subGraph)) jumped = true;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        return jumped;
    }

    // filter に合うステートを、外側からの道筋つきで平らに並べる。ジャンプしたら true
    static bool DrawMatchingStates(AnimStateMachineComponent &fsm, AnimatorComponent *animator,
                                   const AnimStateGraph &graph, const ImGuiTextFilter &filter,
                                   const std::string &prefix)
    {
        bool jumped = false;
        for (const auto &state : graph.states)
        {
            const std::string label = prefix + state.stateName;
            if (filter.PassFilter(state.stateName.c_str()))
            {
                ImGui::PushID(static_cast<int>(state.stateHash));
                const bool clicked = ImGui::Selectable(label.c_str(), IsOnActivePath(fsm, state.stateHash));
                if (clicked && animator && AnimControllerSystem::JumpToState(fsm, *animator, state.stateHash)) jumped = true;
                ImGui::PopID();
            }

            if (state.isSubMachine && state.subGraph)
            {
                if (DrawMatchingStates(fsm, animator, *state.subGraph, filter, label + " > ")) jumped = true;
            }
        }
        return jumped;
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

// ===========================================================================
// AnimParametersComponent（値は実行時に書かれるので保存しない）
// ===========================================================================
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
    static bool CustomGui(AnimParametersComponent &params, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool changed = false;

        const int floatCount   = static_cast<int>(params.floats.size());
        const int boolCount    = static_cast<int>(params.bools.size());
        const int triggerCount = static_cast<int>(params.triggers.size());
        if (floatCount + boolCount + triggerCount == 0)
        {
            ImGui::TextDisabled("パラメータ未設定");
            return changed;
        }

        // 数が多く毎ステップ書き換わるので、既定では件数だけを出して閉じておく
        if (!ImGui::TreeNode("##ParamList", "値の一覧（Float %d / Bool %d / Trigger %d）", floatCount, boolCount,
                             triggerCount))
        {
            return changed;
        }

        // コンポーネントはハッシュしか持たないので、名前は同じ Entity のグラフの定義から引く
        auto       *world = static_cast<CCL::ECS::World *>(worldPtr);
        const auto *fsm   = world ? world->GetComponent<AnimStateMachineComponent>(static_cast<CCL::ECS::EntityID>(entityID))
                                  : nullptr;

        if (ImGui::BeginTable("##AnimParams", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            for (auto &p : params.floats)
            {
                ImGui::PushID(static_cast<int>(p.hash));
                DrawNameCell(fsm, p.hash);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##val", &p.value, 0.05f)) changed = true;
                ImGui::PopID();
            }

            for (auto &p : params.bools)
            {
                ImGui::PushID(static_cast<int>(p.hash));
                DrawNameCell(fsm, p.hash);
                if (ImGui::Checkbox("##val", &p.value)) changed = true;
                ImGui::PopID();
            }

            // トリガーは立っている間だけ並ぶ（先行入力の残り時間）
            for (const auto &t : params.triggers)
            {
                DrawNameCell(fsm, t.hash);
                ImGui::TextDisabled("残り %.2f 秒", t.lifetime);
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();

        return changed;
    }

    // 表の 1 行を始めて名前を書き、値の列へ移る
    // グラフに定義のないパラメータ（コードだけが書くもの）はハッシュで出す
    static void DrawNameCell(const AnimStateMachineComponent *fsm, uint32_t hash)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        const AnimParamDef *def = nullptr;
        if (fsm && fsm->graph)
        {
            for (const auto &d : fsm->graph->parameters)
                if (d.hash == hash) def = &d;
        }

        if (def) ImGui::TextUnformatted(def->name.c_str());
        else ImGui::TextDisabled("0x%08X", hash);

        ImGui::TableNextColumn();
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