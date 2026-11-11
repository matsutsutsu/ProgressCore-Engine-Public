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
// シリアライズ
//   assetHash はパスに戻せないので、ResourceManager で引いたパスを保存する
//   generateLod は true のときだけ書く（既存のプレハブやシーンを保存し直しても差分を出さない）
// ============================================================================
namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<ModelComponent>
    {
        static void Save(json &j, const ModelComponent &comp, SaveContext * /*ctx*/)
        {
            j["assetPath"] = ResourceManager::Instance().GetAssetPath(comp.assetHash);
            if (comp.generateLod)
            {
                j["generateLod"] = true;
            }
        }
        static void Load(const json &j, ModelComponent &comp, LoadContext * /*ctx*/)
        {
            // SetModel が LOD の予約に使うので、モデルより先に読む
            if (j.contains("generateLod") && j["generateLod"].is_boolean())
            {
                comp.generateLod = j["generateLod"].get<bool>();
            }

            if (j.contains("assetPath") && j["assetPath"].is_string())
            {
                comp.SetModel(j["assetPath"].get<std::string>().c_str());
            }
        }
    };
} // namespace ComponentMetaJson

// ============================================================================
// ComponentMeta<ModelComponent>
// ============================================================================
template <>
struct ComponentMeta<ModelComponent>
{
    static constexpr const char *displayName       = "Model";
    static constexpr const char *category          = CCL::Categories::Rendering;
    static constexpr int         sortPriority      = 10; // Transform の直後に並べる
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ModelComponent &comp,
                          unsigned long long /*entityID*/ = 0, void * /*worldPtr*/ = nullptr)
    {
        bool        changed     = false;
        std::string currentPath = ResourceManager::Instance().GetAssetPath(comp.assetHash);

        ImGui::TextDisabled("現在のモデル:");
        if (currentPath.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "(なし / 空)");
        else
            ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.6f, 1.0f), "%s", currentPath.c_str());

        ImGui::Separator();

        if (ImGui::Button("モデルを読み込む...", ImVec2(-1, 0)))
        {
            if (auto path = FileDialog::OpenPreset("Model", "モデルを選択",
                                                   Graphics::Instance().GetWindowHandle()))
            {
                std::string relPath = FileDialog::ConvertToRelativePath(*path);
                comp.SetModel(relPath.c_str());
                changed = true;
            }
        }
        if (!currentPath.empty() && ImGui::Button("モデルをクリア", ImVec2(-1, 0)))
        {
            comp.SetModel("");
            changed = true;
        }

        // --- LOD ---
        ImGui::Separator();
        if (ImGui::Checkbox("LOD を生成", &comp.generateLod))
        {
            changed = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("ドローンのように大量に並べるモデルで使う。\n"
                              "遠くの個体を粗い形で描いて軽くする。\n"
                              "切り替えは、次にモデルを読み込んだときから効く。");
        }

        // 今の共有データに LOD があれば、段ごとの三角形数と作るのにかかった時間を出す
        if (const Model *model = comp.GetModel())
        {
            if (const ModelResource *resource = model->GetResource())
            {
                const auto &meshes = resource->GetMeshes();
                for (size_t i = 0; i < meshes.size(); ++i)
                {
                    if (meshes[i].lods.empty())
                    {
                        continue;
                    }

                    std::string line = "メッシュ" + std::to_string(i) + ":";
                    for (const ModelResource::LodLevel &level : meshes[i].lods)
                    {
                        line += " " + std::to_string(level.indexCount / 3);
                    }
                    line += " 三角形";
                    ImGui::TextDisabled("%s", line.c_str());
                }
                if (resource->GetLodBuildMilliseconds() > 0.0f)
                {
                    ImGui::TextDisabled("LOD の生成: %.2f ms", resource->GetLodBuildMilliseconds());
                }
            }
        }
        return changed;
    }
#else
    static bool CustomGui(ModelComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(ModelComponent, "Model")
REGISTER_COMPONENT_EDITOR_GUI(ModelComponent)