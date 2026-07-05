/// @file  NavAgentComponentMeta.cpp
/// @brief NavAgentComponent のシリアライズ/インスペクタ定義。
///        目標位置の設定や、エディタ上での経路計算テスト機能を提供する。

#include "Engine/AI/Navigation/Components/NavAgentComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加: カテゴリ定数

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <>
struct ComponentMeta<NavAgentComponent>
{
    CCL_META_USING_THIS(NavAgentComponent)

    static constexpr const char *displayName       = "Nav Agent";
    static constexpr const char *category          = CCL::Categories::AI; // ★修正: 定数を使用
    static constexpr int         sortPriority      = 150;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(NavAgentComponent &comp, unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        ImGui::SeparatorText("Navigation Target");

        if (ImGui::DragFloat3("Target Position", &comp.targetPosition.x, 0.1f))
        {
            changed = true;
        }

        ImGui::Spacing();

        if (ImGui::Button("Force Path Request", ImVec2(-1, 0)))
        {
            comp.isPathRequested = true;
            changed              = true;
        }

        ImGui::Spacing();
        ImGui::SeparatorText("Debug Info");

        if (comp.isPathRequested)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Status: Path Requested...");
        }
        else
        {
            ImGui::TextDisabled("Status: Idle / Moving");
        }

        ImGui::Text("Waypoints Count: %zu", comp.waypoints.size());
        ImGui::Text("Current Index: %d", comp.currentWaypointIndex);
        ImGui::Text("currentDesiredSpeed: %.2f", comp.currentDesiredSpeed);
        ImGui::Text("stopDistance: %.2f", comp.stopDistance);

        return changed;
    }
#else
    static bool CustomGui(NavAgentComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(targetPosition, "Target Position", "AI") // フィールドの小カテゴリは生文字列のままで運用
        };
        return fields;
    }
};

REGISTER_COMPONENT(NavAgentComponent, "NavAgent")
REGISTER_COMPONENT_EDITOR_GUI(NavAgentComponent)