#include "DistractionItemComponent.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Core/Math/StringHash.h" // プロジェクトのHashString関数

#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif


template <> struct ComponentMeta<DistractionItemComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Distraction Item (Sound)";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(DistractionItemComponent& comp, unsigned long long entityID = 0, void* worldPtr = nullptr) {
        bool changed = false;

        ImGui::SeparatorText("アイテム設定");

        changed |= ImGui::DragFloat("音の届く半径(m)", &comp.volumeRadius, 0.5f, 1.0f, 100.0f, "%.1f m");

        changed |= ImGui::DragFloat("跳ね反応しきい値(m)", &comp.bounceVelocityThreshold, 0.5f, 1.0f, 100.0f, "%.1f m");

        ImGui::Spacing();
        ImGui::SeparatorText("FMOD サウンドイベント");

        // --- FMOD パス入力 UI ---
        static char pathBuffer[256] = "";
        ImGui::Text("FMODイベントパス");
        if (ImGui::InputText("##FMODPath", pathBuffer, sizeof(pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (strlen(pathBuffer) > 0) {
                // Enterキーが押されたらハッシュ化して保存
                comp.fmodEventHash = CCL::Utils::HashString(pathBuffer);
                changed = true;
                pathBuffer[0] = '\0'; // 入力欄をクリア
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("例: event:/SFX/ItemDrop\nFMODパスを入力して[Enter]で適用します。");
        }

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Hash ID: 0x%08X", comp.fmodEventHash);


        ImGui::Spacing();
        ImGui::SeparatorText("実行時ステータス");

        if (comp.hasTriggered) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[ 発音済み ]");
        }
        else {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "[ 発音待機 ]");
        }

        changed |= ImGui::Checkbox("発音済みフラグ", &comp.hasTriggered);

        return changed;
    }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_FLOAT(DistractionItemComponent, volumeRadius, "volumeRadius", "音の届く半径", 0.5f, 1.0f, 100.0f, "Settings"),

            // ★追加: ハッシュ値をセーブデータ(JSON)に保存する
            // UInt32 の保存マクロがない場合は INT を代用するか、エンジンの仕様に合わせます
            { "FMODハッシュ", "fmodEventHash", FieldKind::Int, offsetof(DistractionItemComponent, fmodEventHash), 1.0f, 0.0f, 0.0f, "Audio", nullptr, 0, true },

            META_FIELD_FLOAT(DistractionItemComponent, bounceVelocityThreshold, "bounceVelocityThreshold", "跳ね反応しきい値", 0.5f, 1.0f, 100.0f, "Settings"),
        };
        return fields;
    }
};

REGISTER_COMPONENT(DistractionItemComponent, "DistractionItem")
REGISTER_COMPONENT_EDITOR_GUI(DistractionItemComponent)