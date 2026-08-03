/// @file  StateTreeComponentMeta.cpp
/// @brief StateTreeComponent のシリアライズ / インスペクタ定義。

#include "Game/Logics/AI/StateTree/Core/StateTreeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Platform/Core/Logger.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include <imgui.h>
#endif

namespace ComponentMetaJson {
    template <> struct ComponentSerializer<StateTreeComponent> {
        static void Save(json& j, const StateTreeComponent& c, SaveContext* /*ctx*/) {
            j["assetName"] = c.assetName;
        }
        static void Load(const json& j, StateTreeComponent& c, LoadContext* /*ctx*/) {
            if (j.contains("assetName"))
                c.assetName = j["assetName"].get<std::string>();
        }
    };
}

template <> struct ComponentMeta<StateTreeComponent> {
    CCL_META_USING_THIS(StateTreeComponent)

    static constexpr const char* displayName    = "StateTree";
    static constexpr const char* category       = CCL::Categories::AI;
    static constexpr int         sortPriority   = 160;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(StateTreeComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", comp.assetName.c_str());
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("アセット名", buf, sizeof(buf))) {
            comp.assetName = buf;
            comp.assetHandle = CCL::AI::StateTreeHandle{};
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(例: Enemy / Boss)");

        if (comp.assetHandle.IsValid())
            ImGui::TextColored({ 0.4f, 1.0f, 0.4f, 1.0f }, "Handle: %u  OK", comp.assetHandle.value);
        else
            ImGui::TextColored({ 1.0f, 0.5f, 0.3f, 1.0f }, "Handle: 未解決  (ResolveSystem が次フレームでセット)");

        ImGui::Separator();
        if (comp.activeDepth > 0) {
            ImGui::TextDisabled("アクティブ状態:");
            for (int d = 0; d < (int)comp.activeDepth; ++d)
                ImGui::Text("  [%d] states[%u]", d, (unsigned)comp.activeStates[d]);
        } else {
            ImGui::TextDisabled("アクティブ: (なし)");
        }
        return changed;
    }
#else
    static bool CustomGui(StateTreeComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(StateTreeComponent, "StateTree")
REGISTER_COMPONENT_EDITOR_GUI(StateTreeComponent)