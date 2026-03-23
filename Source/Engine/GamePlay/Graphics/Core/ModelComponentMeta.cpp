#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Platform/Dialog.h"
#include <filesystem>

#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

// ============================================================================
// ★追加: ModelComponent 専用の JSON シリアライズ処理
// ロード時に単なる文字列コピーではなく、確実に SetModel() を発火させる！
// ============================================================================
namespace ComponentMetaJson {
    // 保存（Save）時の処理
    template <>
    inline void Serialize<ModelComponent>(json& j, const ModelComponent& comp)
    {
        j["assetPath"] = comp.assetPath;
    }

    // 読込（Load）時の処理
    template <>
    inline void Deserialize<ModelComponent>(const json& j, ModelComponent& comp)
    {
        if (j.contains("assetPath") && j["assetPath"].is_string()) {
            std::string path = j["assetPath"].get<std::string>();
            // ★ここでただ文字を入れるだけでなく、ロード処理を走らせる！
            comp.SetModel(path);
        }
    }
}

// ============================================================================
// Component Meta の定義
// ============================================================================
template <> struct ComponentMeta<ModelComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "3D Model";
    static constexpr bool        hasCustomGui = true;

    static bool CustomGui(ModelComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        // 現在のモデルパスの表示
        ImGui::TextDisabled("Current Model:");
        if (comp.assetPath.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "(None / Empty)");
        }
        else {
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", comp.assetPath.c_str());
        }

        ImGui::Separator();

        // 実行中の動的ロードボタン
        if (ImGui::Button("Load New Model... (Dynamic)", ImVec2(-1, 0))) {
            char filename[256] = {};
            const char* filter = "Model Files\0*.gltf;*.glb;*.fbx;*.obj\0All Files\0*.*\0";

            if (Dialog::OpenFileName(filename, 256, filter, "Select 3D Model", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {

                // 絶対パスを相対パスに安全に変換
                namespace fs = std::filesystem;
                fs::path absPath = filename;
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);

                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

                // モデルを切り替え
                comp.SetModel(finalPath);
                changed = true;
            }
        }

        // モデルのクリアボタン
        if (!comp.assetPath.empty()) {
            if (ImGui::Button("Clear Model (Remove)", ImVec2(-1, 0))) {
                comp.SetModel("");
                changed = true;
            }
        }

        return changed;
    }

    // JSON変換は上の特化テンプレートで直接処理するため、汎用リフレクションは空にしておく
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// ============================================================================
// 究極の自動化
// ============================================================================
REGISTER_COMPONENT(ModelComponent, "Model")