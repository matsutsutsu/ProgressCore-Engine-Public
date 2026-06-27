/// @file  ModelComponentMeta.cpp
/// @brief ModelComponent のシリアライズ/インスペクタ定義。
///        assetHash は保存せず、解決済みの assetPath を保存する (カスタムシリアライズ)。

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include <filesystem>

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include "Engine/Graphics/Resource/ResourceManager.h"

// ============================================================================
// カスタムシリアライズ:
//   ComponentMetaJson::ComponentSerializer<ModelComponent> を特殊化する。
//   これが唯一のカスタマイズ口。フラグもトレイトも不要。
//   ModelComponent は assetHash (実行時ハンドル) を持つが、保存にはパスを使う。
// ============================================================================
namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<ModelComponent> {
        static void Save(json& j, const ModelComponent& comp, SaveContext* /*ctx*/) {
            j["assetPath"] = ResourceManager::Instance().GetAssetPath(comp.assetHash);
        }
        static void Load(const json& j, ModelComponent& comp, LoadContext* /*ctx*/) {
            if (j.contains("assetPath") && j["assetPath"].is_string()) {
                comp.SetModel(j["assetPath"].get<std::string>().c_str());
            }
        }
    };
} // namespace ComponentMetaJson

// ============================================================================
// ComponentMeta<ModelComponent>
//   hasCustomGui = true。Fields() は空 (保存は上の ComponentSerializer が担う)。
// ============================================================================
template <> struct ComponentMeta<ModelComponent> {
    static constexpr const char* displayName = "Model";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ModelComponent& comp,
        unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
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
            if (auto path = FileDialog::OpenPreset("Model", "Select Model",
                Graphics::Instance().GetWindowHandle()))
            {
                std::filesystem::path workDir = std::filesystem::current_path();
                std::filesystem::path relativePath = std::filesystem::relative(*path, workDir);
                comp.SetModel(relativePath.generic_string().c_str());
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

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty; // カスタムシリアライズなので空
        return empty;
    }
};

REGISTER_COMPONENT(ModelComponent, "Model")
REGISTER_COMPONENT_EDITOR_GUI(ModelComponent)