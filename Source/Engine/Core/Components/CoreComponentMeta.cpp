/// @file  CoreComponentMeta.cpp
/// @brief NameComponent のシリアライズ/インスペクタ定義。

#include "Engine/Core/Components/NameComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// パターンC: NameComponent 用カスタムシリアライズ
// ============================================================================
namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<NameComponent> {
        static void Save(json& j, const NameComponent& comp, SaveContext* /*ctx*/) {
            j["name"] = std::string(comp.name);
        }

        static void Load(const json& j, NameComponent& comp, LoadContext* /*ctx*/) {
            if (j.contains("name") && j["name"].is_string()) {
                std::string str = j["name"].get<std::string>();
                strncpy_s(comp.name, str.c_str(), sizeof(comp.name) - 1);
                comp.name[sizeof(comp.name) - 1] = '\0';
            }
        }
    };
} // namespace ComponentMetaJson

// ============================================================================
// ComponentMeta<NameComponent>
// ============================================================================
template <> struct ComponentMeta<NameComponent> {
    static constexpr const char* displayName    = "Name";
    static constexpr const char* category       = CCL::Categories::Meta; // 最も重要なメタ情報
    static constexpr int         sortPriority   = 0; // Transformと同様に最上段に固定
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(NameComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "Entity Name");
        if (ImGui::InputText("##NameInput", comp.name, sizeof(comp.name))) {
            changed = true;
        }
        return changed;
    }
#else
    static bool CustomGui(NameComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(NameComponent, "Name")
REGISTER_COMPONENT_EDITOR_GUI(NameComponent)