// ModelComponent のシリアライズ/インスペクタ定義。
// assetHash は保存せず、解決済みの assetPath を保存する (カスタムシリアライズ)。

#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include "Engine/Graphics/Resource/ResourceManager.h"

// ============================================================================
// カスタムシリアライズ:
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
} 

// ============================================================================
// ComponentMeta<ModelComponent>
// ============================================================================
template <> struct ComponentMeta<ModelComponent> {
    static constexpr const char* displayName    = "Model";
    static constexpr const char* category       = CCL::Categories::Rendering;
    static constexpr int         sortPriority   = 10; // Transformの直後に配置されるべき重要コンポーネント
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ModelComponent& comp,
        unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;
        std::string currentPath = ResourceManager::Instance().GetAssetPath(comp.assetHash);

        ImGui::TextDisabled("現在のモデル:");
        if (currentPath.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "(なし / 空)");
        else
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", currentPath.c_str());

        ImGui::Separator();

        if (ImGui::Button("モデルを読み込む...", ImVec2(-1, 0))) {
            // std::filesystem の依存を排除し、厳格に FileDialog::ConvertToRelativePath を使用
            if (auto path = FileDialog::OpenPreset("Model", "モデルを選択",
                Graphics::Instance().GetWindowHandle()))
            {
                std::string relPath = FileDialog::ConvertToRelativePath(*path);
                comp.SetModel(relPath.c_str());
                changed = true;
            }
        }
        if (!currentPath.empty() && ImGui::Button("モデルをクリア", ImVec2(-1, 0))) {
            comp.SetModel("");
            changed = true;
        }
        return changed;
    }
#else
    static bool CustomGui(ModelComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty; 
        return empty;
    }
};

REGISTER_COMPONENT(ModelComponent, "Model")
REGISTER_COMPONENT_EDITOR_GUI(ModelComponent)