// SkyboxComponent のシリアライズ/インスペクタ定義。
// 空(スカイボックス)と IBL(IEM/PMREM) を1セットで差し替えるための設定。

#include "Engine/Graphics/Components/Lighting/SkyboxComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#include <cstring>
#include <string>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include <imgui.h>
#endif

// ---------------------------------------------------------------------------
// シリアライズ：3つのパスを保存/復元する（これが「シーンごとに空を保存」の要）。
// ---------------------------------------------------------------------------
namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<SkyboxComponent> {
        static void Save(json& j, const SkyboxComponent& c, SaveContext* /*ctx*/) {
            j["skybox"]     = c.skyboxTexturePath;
            j["irradiance"] = c.irradiancePath;
            j["prefilter"]  = c.prefilterPath;
        }
        static void Load(const json& j, SkyboxComponent& c, LoadContext* /*ctx*/) {
            auto copyStr = [](char* dst, const json& j, const char* key) {
                if (j.contains(key)) {
                    std::string s = j[key].get<std::string>();
                    strncpy_s(dst, 256, s.c_str(), _TRUNCATE);
                }
            };
            copyStr(c.skyboxTexturePath, j, "skybox");
            copyStr(c.irradiancePath,    j, "irradiance");
            copyStr(c.prefilterPath,     j, "prefilter");
        }
    };
}

template <> struct ComponentMeta<SkyboxComponent> {
    static constexpr const char* displayName       = "Sky / IBL";
    static constexpr const char* category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority       = 405;
    static constexpr bool        hiddenInInspector  = false;
    static constexpr bool        hasCustomGui       = true;
    static constexpr bool        isSerializable     = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(SkyboxComponent& c,
                          unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;

        ImGui::TextDisabled(u8"空(背景)と IBL(陰影)は同じ空から焼いた3点セットで指定します。\n"
                            u8"空欄なら既定の空にフォールバックします。");
        ImGui::Separator();

        // パス1行分：ラベル左 → 現在パス → [読込...] [クリア]。
        // handle を無効化すると EnvironmentSystem が次フレームで再ロードする。
        auto pathRow = [&](const char* label, char* path, TextureHandle& handle) {
            ComponentMetaImGui::Prop(label);
            ImGui::TextUnformatted(path[0] ? path : u8"(既定)");

            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(u8"読込...##") + label).c_str())) {
                if (auto p = FileDialog::OpenPreset(
                        "Texture", u8"キューブマップ(.dds)を選択",
                        Graphics::Instance().GetWindowHandle())) {
                    std::string rel = FileDialog::ConvertToRelativePath(*p);
                    strncpy_s(path, 256, rel.c_str(), _TRUNCATE);
                    handle  = TextureHandle{}; // 再ロードさせる
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(u8"クリア##") + label).c_str())) {
                path[0] = '\0';
                handle  = TextureHandle{};
                changed = true;
            }
        };

        pathRow(u8"空(Skybox)",      c.skyboxTexturePath, c.skyboxHandle);
        pathRow(u8"IBL拡散(IEM)",    c.irradiancePath,    c.irradianceHandle);
        pathRow(u8"IBL鏡面(PMREM)",  c.prefilterPath,     c.prefilterHandle);

        ImGui::Separator();
        if (ImGui::Button(u8"再ロード (Reload All)")) {
            c.skyboxHandle     = TextureHandle{};
            c.irradianceHandle = TextureHandle{};
            c.prefilterHandle  = TextureHandle{};
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(u8"外部ツールで焼き直した後に押す");

        return changed;
    }
#else
    static bool CustomGui(SkyboxComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// 登録名は必ず displayName から取る。ここに文字列を直書きすると、インスペクタの
// 見出し(displayName)と JSONキー/追加メニュー(登録名)が食い違い、プレハブに
// 見出しどおりのキーを書いても無言でコンポーネントが付かなくなる。
// （COMPONENT_META_END マクロが自動でやっている事を、手書きメタでも守る）
REGISTER_COMPONENT(SkyboxComponent, ComponentMeta<SkyboxComponent>::displayName)
REGISTER_COMPONENT_EDITOR_GUI(SkyboxComponent)
