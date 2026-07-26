/// @file  AudioComponentMeta.cpp
/// @brief AudioEmitterComponent のシリアライズ/インスペクタ定義。

#include "Engine/Audio/Components/AudioEmitterComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include "Engine/Core/Math/StringHash.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/ECS/Core/CCL_World.h"              // World からオーディオ資源を引く
#include "Engine/Audio/FmodIntegration/IAudioAPI.h" // イベント名一覧（カタログ）
#include <memory>
#include <vector>
#include <cstdio>
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
    // イベント名を確定させる小ヘルパー：名前を保存し、ハッシュを再計算する。
    static void ApplyEventName(AudioEmitterComponent &comp, const std::string &name)
    {
        comp.eventName = name;
        comp.eventHash = name.empty() ? 0u : CCL::Utils::HashString(name.c_str());
    }

    static bool CustomGui(AudioEmitterComponent &comp, unsigned long long /*entityID*/ = 0, void *world = nullptr)
    {
        bool changed = false;
        ImGui::PushID("AudioEmitterComponent_UI");

        // --- カタログ（AudioConfig にロード済みのイベント名一覧）を取得 ---
        std::vector<std::string> names;
        if (world)
        {
            auto *w = static_cast<CCL::ECS::World *>(world);
            if (w->HasResource<std::shared_ptr<IAudioAPI>>())
                if (auto &api = w->GetResource<std::shared_ptr<IAudioAPI>>())
                    names = api->GetEventNames();
        }

        // --- ① ドロップダウン（カタログから選ぶ。手打ち不要） ---
        ImGui::Text("サウンド");
        const std::string current = comp.eventName.empty() ? std::string("<未設定>") : comp.eventName;
        if (ImGui::BeginCombo("##SoundCombo", current.c_str()))
        {
            if (names.empty())
                ImGui::TextDisabled("AudioConfig に登録がありません");
            for (const std::string &n : names)
            {
                const bool selected = (n == comp.eventName);
                if (ImGui::Selectable(n.c_str(), selected))
                {
                    ApplyEventName(comp, n);
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Assets/Config/AudioConfigDXTK.json に登録した音から選びます。");

        // --- ② 手入力フォールバック（カタログに無い名前を直接指定したいとき） ---
        char buf[256] = {};
        std::snprintf(buf, sizeof(buf), "%s", comp.eventName.c_str());
        ImGui::TextDisabled("または名前を直接指定");
        if (ImGui::InputText("##SoundName", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            ApplyEventName(comp, buf);
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("例: bgm:/game を入力して[Enter]。");

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Hash: 0x%08X", comp.eventHash);
        ImGui::Spacing();

        // 音量（0=無音, 1=原音）。実行中に動かすとその場で反映される。
        if (ImGui::SliderFloat("音量", &comp.volume, 0.0f, 10.0f, "%.2f")) changed = true;

        if (ImGui::Checkbox("生成時に自動再生", &comp.autoPlay)) changed = true;

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
        // 保存の正は eventName（人間可読）。eventHash は実行時に名前から導出するので保存しない。
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(eventName, "サウンド名", "オーディオ")
            CFIELD(volume, "音量", "オーディオ")
            CFIELD(autoPlay, "自動再生", "オーディオ")};
        return fields;
    }
};

REGISTER_COMPONENT(AudioEmitterComponent, "AudioEmitterComponent")
REGISTER_COMPONENT_EDITOR_GUI(AudioEmitterComponent)
