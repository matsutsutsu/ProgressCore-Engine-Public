#include "ECS/Core/CCL_World.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
//#include "Engine/Serialization/ComponentSerializers.h"
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Platform/Dialog.h"

// ===================================================================
// 1. コンポーネントの「取扱説明書（メタデータ）」の定義
// ===================================================================
template <> struct ComponentMeta<AnimatorComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Animator";

    // ★ここを true にしておくだけで、自動的に下の CustomGui が呼ばれます
    static constexpr bool        hasCustomGui = true;

    // エディタ（Inspector）に表示する専用のUI
    static bool CustomGui(AnimatorComponent& anim, unsigned long long entityID, void* worldPtr)
    {
        bool changed = false;

        ImGui::DragFloat("Speed", &anim.playbackSpeed, 0.05f, -5.0f, 5.0f);
        if (ImGui::Checkbox("Loop", &anim.isLoop)) changed = true;
        ImGui::Separator();

        // ============================================================
        // アニメーションの動的プレビュー機能
        // ============================================================
        ImGui::TextDisabled("Dynamic Preview");
        if (ImGui::Button("Load & Play Sequence... (JSON)", ImVec2(-1, 0))) {
            char filename[256] = {};
            if (Dialog::OpenFileName(filename, 256, "JSON Files\0*.json\0", "Select Anim Sequence", Graphics::Instance().GetWindowHandle()) == DialogResult::OK) {
                
                // 絶対パスを相対パスに変換
                namespace fs = std::filesystem;
                fs::path absPath = filename;
                fs::path currentPath = fs::current_path();
                std::error_code ec;
                fs::path relPath = fs::relative(absPath, currentPath, ec);
                std::string finalPath = (!ec && !relPath.empty()) ? relPath.generic_string() : filename;

                // ResourceManager経由で JSON の「台本」をロード
                const AnimSequence* loadedSeq = ResourceManager::Instance().LoadAnimSequence(finalPath.c_str());
                
                if (loadedSeq) {
                    // ★ Animator の Play を強制的に呼び出し、今すぐ再生させる！
                    anim.Play(loadedSeq, true, 1.0f);
                    changed = true;
                }
            }
        }
        ImGui::Separator();

        // 現在の台本情報を表示
        if (anim.currentSequence) {
            ImGui::Text("Sequence: %s", anim.currentSequence->sequenceName.c_str());
            ImGui::Text("Target Anim: %s", anim.currentSequence->targetAnimName.c_str());
            ImGui::Text("Time: %.2f / %.2f", anim.currentTimer, anim.currentSequence->duration);
        }
        else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "No Sequence Playing");
        }

        ImGui::Text("Finished: %s", anim.isFinished ? "True" : "False");

        return changed;
    }

    // JSON保存用の変数リスト
    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(AnimatorComponent, playbackSpeed, "playbackSpeed", "Playback Speed", 0.05f, -5.0f, 5.0f, "Playback"),
            META_FIELD_BOOL(AnimatorComponent, isLoop, "isLoop", "Loop", "Playback")
        };
        return fields;
    }
};


REGISTER_COMPONENT(AnimatorComponent, "Animator")