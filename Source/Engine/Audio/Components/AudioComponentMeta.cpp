/// @file  AudioComponentMeta.cpp
/// @brief AudioEmitterComponent のシリアライズ/インスペクタ定義。

#include "Engine/Audio/Components/AudioEmitterComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include "Engine/Core/Math/StringHash.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <>
struct ComponentMeta<AudioEmitterComponent>
{
    CCL_META_USING_THIS(AudioEmitterComponent)

    static constexpr const char *displayName       = "Audio Emitter";
    static constexpr const char *category          = CCL::Categories::Audio; // ★修正
    static constexpr int         sortPriority      = 400;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AudioEmitterComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::PushID("AudioEmitterComponent_UI");

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Hash ID: 0x%08X", comp.eventHash);

        static char pathBuffer[256] = "";

        ImGui::Text("FMODイベントパス");
        if (ImGui::InputText("##FMODPath", pathBuffer, sizeof(pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (strlen(pathBuffer) > 0)
            {
                comp.eventHash = CCL::Utils::HashString(pathBuffer);
                changed        = true;
            }
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("例: event:/SFX/Shoot\nFMODパスを入力して[Enter]で適用します。");
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("生成時に自動再生", &comp.autoPlay))
        {
            changed = true;
        }

        ImGui::BeginDisabled();
        ImGui::Checkbox("再生中(実行時)", &comp.isPlaying);
        ImGui::EndDisabled();

        ImGui::PopID();

        return changed;
    }
#else
    static bool CustomGui(AudioEmitterComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(eventHash, "イベントハッシュ", "オーディオ")
                CFIELD(autoPlay, "自動再生", "オーディオ")};
        return fields;
    }
};

REGISTER_COMPONENT(AudioEmitterComponent, "AudioEmitterComponent")
REGISTER_COMPONENT_EDITOR_GUI(AudioEmitterComponent)