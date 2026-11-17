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
// シリアライズ: 3つのパスを保存・復元する。これでシーンごとに空を持てる
// ---------------------------------------------------------------------------
namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<SkyboxComponent>
    {
        static void Save(json &j, const SkyboxComponent &c, SaveContext * /*ctx*/)
        {
            j["skybox"]       = c.skyboxTexturePath;
            j["irradiance"]   = c.irradiancePath;
            j["prefilter"]    = c.prefilterPath;
            j["skyIntensity"] = c.skyIntensity;
        }
        static void Load(const json &j, SkyboxComponent &c, LoadContext * /*ctx*/)
        {
            auto copyStr = [](char *dst, const json &j, const char *key) {
                if (j.contains(key))
                {
                    std::string s = j[key].get<std::string>();
                    strncpy_s(dst, 256, s.c_str(), _TRUNCATE);
                }
            };
            copyStr(c.skyboxTexturePath, j, "skybox");
            copyStr(c.irradiancePath, j, "irradiance");
            copyStr(c.prefilterPath, j, "prefilter");

            // 明るさを足す前に保存したシーンにはないので、そのときは既定（1.0）のまま
            if (j.contains("skyIntensity"))
            {
                c.skyIntensity = j["skyIntensity"].get<float>();
            }
        }
    };
}

template <>
struct ComponentMeta<SkyboxComponent>
{
    static constexpr const char *displayName       = "Sky / IBL";
    static constexpr const char *category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority      = 405;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(SkyboxComponent &c,
                          unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        bool changed = false;

        ImGui::TextDisabled(u8"空(背景)と IBL(陰影)は同じ空から焼いた3点セットで指定します。\n"
                            u8"空欄なら既定の空を使います。");
        ImGui::Separator();

        changed |= ComponentMetaImGui::DragFloat(u8"空の明るさ", &c.skyIntensity, 0.01f, 0.0f, 4.0f,
                                                 u8"空を見るとまぶしいときに下げる。キャラや地形の陰影は変わらない");
        ImGui::Separator();

        // パス1行分: ラベル → 現在のパス → [読込...] [クリア]
        // handle を無効にすると、EnvironmentSystem が次のフレームで読み直す
        auto pathRow = [&](const char *label, char *path, TextureHandle &handle) {
            ComponentMetaImGui::Prop(label);
            ImGui::TextUnformatted(path[0] ? path : u8"(既定)");

            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(u8"読込...##") + label).c_str()))
            {
                if (auto p = FileDialog::OpenPreset(
                        "Texture", u8"キューブマップ(.dds)を選択",
                        Graphics::Instance().GetWindowHandle()))
                {
                    std::string rel = FileDialog::ConvertToRelativePath(*p);
                    strncpy_s(path, 256, rel.c_str(), _TRUNCATE);
                    handle  = TextureHandle{}; // 再ロードさせる
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton((std::string(u8"クリア##") + label).c_str()))
            {
                path[0] = '\0';
                handle  = TextureHandle{};
                changed = true;
            }
        };

        pathRow(u8"空(Skybox)", c.skyboxTexturePath, c.skyboxHandle);
        pathRow(u8"IBL拡散(IEM)", c.irradiancePath, c.irradianceHandle);
        pathRow(u8"IBL鏡面(PMREM)", c.prefilterPath, c.prefilterHandle);

        ImGui::Separator();
        if (ImGui::Button(u8"再ロード (Reload All)"))
        {
            c.skyboxHandle     = TextureHandle{};
            c.irradianceHandle = TextureHandle{};
            c.prefilterHandle  = TextureHandle{};
            changed            = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(u8"外部ツールで焼き直した後に押す");

        return changed;
    }
#else
    static bool CustomGui(SkyboxComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// 登録名は displayName から取る。文字列を直接書くと、インスペクタの見出し（displayName）と
// JSON のキー・追加メニュー（登録名）が食い違い、見出しどおりのキーをプレハブに書いても
// コンポーネントが付かない（エラーも出ない）。COMPONENT_META_END マクロは自動でこうしている
REGISTER_COMPONENT(SkyboxComponent, ComponentMeta<SkyboxComponent>::displayName)
REGISTER_COMPONENT_EDITOR_GUI(SkyboxComponent)
