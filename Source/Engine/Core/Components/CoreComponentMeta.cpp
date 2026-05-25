#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Core/Components/NameComponent.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// NameComponent 専用 JSON シリアライズ (明示的テンプレート特化)
//
//   ★ 修正点: ctx 引数を追加してプライマリテンプレートのシグネチャに合わせる。
//     旧: Serialize<NameComponent>(json& j, const NameComponent& comp)
//     新: Serialize<NameComponent>(json& j, const NameComponent& comp, SaveContext* ctx)
//
//   NameComponent は char 配列を持ち Fields() で表現できないため、
//   手書きの変換ロジックが必要。ctx は不使用だが省略不可。
// ============================================================================
namespace ComponentMetaJson {

    template <>
    void Serialize<NameComponent>(json& j, const NameComponent& comp, SaveContext* /*ctx*/)
    {
        j["name"] = std::string(comp.name);
    }

    template <>
    void Deserialize<NameComponent>(const json& j, NameComponent& comp, LoadContext* /*ctx*/)
    {
        // NameComponent は EntityLink を持たないため ctx は不要
        if (j.contains("name") && j["name"].is_string()) {
            std::string str = j["name"].get<std::string>();
            strncpy_s(comp.name, str.c_str(), sizeof(comp.name) - 1);
            comp.name[sizeof(comp.name) - 1] = '\0';
        }
    }

} // namespace ComponentMetaJson

// ============================================================================
// ComponentMeta<NameComponent>
// ============================================================================
template <> struct ComponentMeta<NameComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Name";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(NameComponent& comp,
        unsigned long long /*entityID*/ = 0,
        void* /*world*/ = nullptr)
    {
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

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// ============================================================================
// 登録
// ============================================================================
REGISTER_COMPONENT(NameComponent, "Name")
REGISTER_COMPONENT_EDITOR_GUI(NameComponent)