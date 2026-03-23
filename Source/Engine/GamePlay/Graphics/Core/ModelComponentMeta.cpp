#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Platform/Dialog.h"
#include <filesystem>

#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

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
            // 対応する拡張子（GLTF, GLB, FBX等）を指定
            const char* filter = "Model Files\0*.gltf;*.glb;*.fbx;*.obj\0All Files\0*.*\0";

            if (Dialog::OpenFileName(filename, 256, filter, "Select 3D Model", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {

                // 絶対パスをプロジェクト基準の相対パスに安全に変換
                namespace fs = std::filesystem;
                fs::path absPath = filename;
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);

                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

                // ★ここが Data-Driven の真骨頂：パスを渡すだけで瞬時にモデルが切り替わる
                comp.SetModel(finalPath);
                changed = true;
            }
        }

        // モデルのクリアボタン
        if (!comp.assetPath.empty()) {
            if (ImGui::Button("Clear Model (Remove)", ImVec2(-1, 0))) {
                comp.SetModel(""); // 空文字を渡すと内部で破棄される
                changed = true;
            }
        }

        return changed;
    }

    // JSONシリアライズの設定
    static const std::vector<FieldDescriptor>& Fields()
    {
        // 文字列 (std::string) 用のマクロが未定義の環境も考慮し、直接構造体を初期化して確実性を担保します
        static const std::vector<FieldDescriptor> fields = {
            {
                "Asset Path",             // name (UI非表示用だが設定)
                "assetPath",              // jsonName (JSONのキー)
                FieldKind::String,        // kind (文字列型)
                offsetof(ModelComponent, assetPath), // offset
                0.0f, 0.0f, 0.0f,         // UI parameters (文字列には未使用)
                "Model",                  // category
                nullptr, 0, true          // enums & serialize flag
            }
        };
        return fields;
    }
};

// ============================================================================
// 究極の自動化：これだけでJSONとImGuiの両方に登録される
// ============================================================================
REGISTER_COMPONENT(ModelComponent, "Model")