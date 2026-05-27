#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include <filesystem>

#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include "Engine/Graphics/Resource/ResourceManager.h"

// ============================================================================
// ModelComponent 専用 JSON シリアライズ (明示的テンプレート特化)
//
//   ★ 修正点: 旧シグネチャ
//       Serialize<ModelComponent>(json& j, const ModelComponent& comp)
//     を新シグネチャに合わせる:
//       Serialize<ModelComponent>(json& j, const ModelComponent& comp, SaveContext* ctx)
//
//   ctx は使わないが、プライマリテンプレートのシグネチャと一致しないと
//   「テンプレートの特化ではありません」エラーになる。
// ============================================================================
namespace ComponentMetaJson {

    template <>
    void Serialize<ModelComponent>(json& j, const ModelComponent& comp, SaveContext* /*ctx*/)
    {
        std::string path = ResourceManager::Instance().GetAssetPath(comp.assetHash);
        j["assetPath"] = path;
    }

    template <>
    void Deserialize<ModelComponent>(const json& j, ModelComponent& comp, LoadContext* /*ctx*/)
    {
        // ModelComponent は EntityLink を持たないため ctx は不要
        if (j.contains("assetPath") && j["assetPath"].is_string()) {
            comp.SetModel(j["assetPath"].get<std::string>().c_str());
        }
    }

} // namespace ComponentMetaJson

// ============================================================================
// ComponentMeta<ModelComponent>
// ============================================================================
template <> struct ComponentMeta<ModelComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Model";
    static constexpr bool        hasCustomGui = true;
    // ★ Fields() が空で、上の explicit specialization が Serialize/Deserialize を
    //    直接上書きする。HasCustomSerialize は false のまま。
    //    (explicit specialization は HasCustomSerialize フラグを見ず、
    //     直接そのシグネチャを呼ぶため)

#ifdef ENABLE_EDITOR
    static bool CustomGui(ModelComponent& comp,
        unsigned long long /*entityID*/ = 0,
        void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        std::string currentPath = ResourceManager::Instance().GetAssetPath(comp.assetHash);

        ImGui::TextDisabled("Current Model:");
        if (currentPath.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "(None / Empty)");
        else
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", currentPath.c_str());

        ImGui::Separator();

        if (ImGui::Button("Load New Model...", ImVec2(-1, 0))) {
            if (auto path = Engine::FileDialog::OpenPreset("Model", "Select Model",
                Graphics::Instance().GetWindowHandle()))
            {
                comp.SetModel(path->string().c_str());
                changed = true;
            }
        }

        if (!currentPath.empty() && ImGui::Button("Clear Model", ImVec2(-1, 0))) {
            comp.SetModel("");
            changed = true;
        }

        return changed;
    }
#else
    static bool CustomGui(ModelComponent&, unsigned long long, void*) { return false; }
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
REGISTER_COMPONENT(ModelComponent, "Model")
REGISTER_COMPONENT_EDITOR_GUI(ModelComponent)