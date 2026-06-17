/// @file  AudioComponentMeta.cpp
/// @brief AudioEmitterComponent のシリアライズ/インスペクタ定義。
///        FMODイベントの再生制御と、パスのハッシュ化UIを提供する。

#include "Engine/Audio/Components/AudioEmitterComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Core/Math/StringHash.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <> struct ComponentMeta<AudioEmitterComponent> {
    CCL_META_USING_THIS(AudioEmitterComponent)

        static constexpr const char* displayName = "Audio Emitter";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(AudioEmitterComponent& comp, unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::PushID("AudioEmitterComponent_UI");

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Hash ID: 0x%08X", comp.eventHash);

        static char pathBuffer[256] = "";

        ImGui::Text("FMOD Event Path");
        if (ImGui::InputText("##FMODPath", pathBuffer, sizeof(pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (strlen(pathBuffer) > 0) {
                comp.eventHash = CCL::Utils::HashString(pathBuffer);
                changed = true;
                pathBuffer[0] = '\0';
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Example: event:/SFX/Shoot\nType the FMOD path and press [Enter] to apply.");
        }

        ImGui::Spacing();

        if (ImGui::Checkbox("Auto Play on Spawn", &comp.autoPlay)) {
            changed = true;
        }

        ImGui::BeginDisabled();
        ImGui::Checkbox("Is Playing (Runtime)", &comp.isPlaying);
        ImGui::EndDisabled();

        ImGui::PopID();

        return changed;
    }
#else
    static bool CustomGui(AudioEmitterComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(eventHash, "Event Hash", "Audio")
            CFIELD(autoPlay,  "Auto Play",  "Audio")
        };
        return fields;
    }
};

REGISTER_COMPONENT(AudioEmitterComponent, "AudioEmitterComponent")
REGISTER_COMPONENT_EDITOR_GUI(AudioEmitterComponent)