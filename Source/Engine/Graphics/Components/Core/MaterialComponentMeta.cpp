/// @file  MaterialComponentMeta.cpp
/// @brief MaterialComponent のシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include <vector>

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// =====================================================================
// シリアライズ（A案＝アセット参照方式）
//   実行時専用ハンドル(overrideMaterials)は保存できないため、
//   代わりにスロットごとの .mat.json パス(overridePaths)を保存する。
//   ロード時はパスから LoadMaterialAsset で復元し、ハンドルを埋め直す。
//   ※ ComponentSerializer<T> は REGISTER_COMPONENT より前に見えている必要がある
//      (ComponentRegistry の Register<T> が実体化するため)。
// =====================================================================
namespace ComponentMetaJson
{
template <>
struct ComponentSerializer<MaterialComponent>
{
    static void Save(json &j, const MaterialComponent &c, CCL::Serialization::SaveContext *)
    {
        // スロット数は overrideMaterials 基準。パスは並走列から取る（無ければ空）。
        json paths = json::array();
        for (size_t i = 0; i < c.overrideMaterials.size(); ++i)
        {
            const char *p = (i < c.overridePaths.size()) ? c.overridePaths[i].c_str() : "";
            paths.push_back(p ? p : "");
        }
        j["paths"] = paths;
        j["customParams"] =
            json::array({c.customParams.x, c.customParams.y, c.customParams.z, c.customParams.w});
    }

    static void Load(const json &j, MaterialComponent &c, CCL::Serialization::LoadContext *)
    {
        // 同じコンポーネントへ再ロードされた場合に備え、古いハンドルを先に返す。
        // （「既定に戻す」ボタンと同じ後始末。ここだけ抜けているとプール枠が漏れる）
        for (auto handle : c.overrideMaterials)
            ResourceManager::Instance().UnloadMaterial(handle);

        c.overrideMaterials.clear();
        c.overridePaths.clear();

        if (j.contains("paths") && j["paths"].is_array())
        {
            const auto &paths = j["paths"];
            c.ResizeSlots(paths.size());
            for (size_t i = 0; i < paths.size(); ++i)
            {
                std::string path = paths[i].is_string() ? paths[i].get<std::string>() : std::string();
                c.overridePaths[i] = path;
                if (!path.empty())
                {
                    // パスが指す .mat.json を解決（内部でテクスチャも遅延ロード）。
                    MaterialHandle h = ResourceManager::Instance().LoadMaterialAsset(path);
                    c.overrideMaterials[i] = h; // 失敗時は無効ハンドル＝共有マテリアルへフォールバック
                }
            }
        }

        if (j.contains("customParams") && j["customParams"].is_array() && j["customParams"].size() >= 4)
        {
            const auto &a  = j["customParams"];
            c.customParams = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>()};
        }
    }
};
} // namespace ComponentMetaJson

template <>
struct ComponentMeta<MaterialComponent>
{
    static constexpr const char *displayName    = "Material";
    static constexpr const char *category       = CCL::Categories::Rendering;
    static constexpr int         sortPriority   = 20; // Modelの直後に配置
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    // シェーダー名リストと、現在の shaderHash に対応する index を集める共通処理。
    //   （旧コードは同じ収集を3箇所にコピペしていたため 1 箇所に集約）
    struct ShaderList
    {
        std::vector<const char *> names;
        int                       current = -1; // 見つからなければ -1
    };
    static ShaderList GatherShaders(uint32_t currentHash)
    {
        ShaderList out;
        const auto &registered = ShaderRegistry::Instance().GetRegisteredNames();
        out.names.reserve(registered.size());
        for (size_t s = 0; s < registered.size(); ++s)
        {
            out.names.push_back(registered[s].first.c_str());
            if (registered[s].second == currentHash) out.current = (int)s;
        }
        return out;
    }

    static bool CustomGui(MaterialComponent &compRef, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool               changed = false;
        MaterialComponent *comp    = &compRef;
        auto              *world   = static_cast<CCL::ECS::World *>(worldPtr);
        auto              &rm      = ResourceManager::Instance();

        // ----- customParams（シェーダー instanceData に直結する共通4値）-----
        if (ImGui::DragFloat4("customParams", &comp->customParams.x, 0.01f))
            changed = true;
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("シェーダーの instanceData.customParams に直結する共通4値。\n"
                              "用途はシェーダー依存（点滅強度・UVスクロール等）。");
        ImGui::Separator();

        // ----- 上書き未使用（共有マテリアル）状態 -----
        if (comp->overrideMaterials.empty())
        {
            ImGui::TextDisabled("共有マテリアルを使用中(既定)");
            bool hasModel = false;
            if (world)
            {
                auto *modelComp = world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
                if (modelComp && modelComp->GetModel())
                {
                    hasModel = true;
                    if (ImGui::Button("全マテリアルを上書き(編集)"))
                    {
                        const auto &meshes = modelComp->GetModel()->GetMeshes();
                        comp->ResizeSlots(meshes.size());
                        for (size_t i = 0; i < meshes.size(); ++i)
                        {
                            auto          *originalMat = meshes[i].material;
                            MaterialHandle newHandle   = rm.CreateMaterial();
                            MaterialData  *newData     = rm.GetMaterial(newHandle);
                            if (originalMat && originalMat->data && newData)
                            {
                                *newData = *originalMat->data;
                            }
                            comp->SetData(i, newHandle);
                        }
                        changed = true;
                    }
                }
            }
            if (!hasModel && ImGui::Button("空のマテリアルリストを作成"))
            {
                MaterialHandle emptyHandle = rm.CreateMaterial();
                comp->ResizeSlots(1);
                comp->SetData(0, emptyHandle);
                changed = true;
            }
            return changed;
        }

        if (ImGui::Button("既定に戻す(変更を破棄)"))
        {
            for (auto handle : comp->overrideMaterials)
                rm.UnloadMaterial(handle);
            comp->overrideMaterials.clear();
            comp->overridePaths.clear();
            return true;
        }

        // ----- 一括操作 -----
        ImGui::Separator();
        if (ImGui::CollapsingHeader("一括操作(全スロットに適用)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ShaderList sl = GatherShaders(0);
            if (!sl.names.empty())
            {
                // static を廃止し ImGui のper-IDストレージへ。
                //   （複数エンティティ間で選択が混線するバグを回避）
                ImGuiStorage *store = ImGui::GetStateStorage();
                ImGuiID       key   = ImGui::GetID("batchShaderIdx");
                int           idx   = store->GetInt(key, 0);
                if (idx >= (int)sl.names.size()) idx = 0;

                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("##BatchShader", &idx, sl.names.data(), (int)sl.names.size()))
                    store->SetInt(key, idx);
                ImGui::SameLine();

                if (ImGui::Button("全スロットにシェーダー適用"))
                {
                    const auto &registered  = ShaderRegistry::Instance().GetRegisteredNames();
                    uint32_t    targetHash  = registered[idx].second;
                    for (auto handle : comp->overrideMaterials)
                    {
                        if (!handle.IsValid()) continue;
                        if (MaterialData *m = rm.GetMaterial(handle)) m->shaderHash = targetHash;
                    }
                    changed = true;
                }
            }
            else
            {
                ImGui::TextDisabled("シェーダー未登録(ShaderManifest未読込?)");
            }
        }

        // ----- スロット一覧 -----
        ImGui::Separator();
        ImGui::Text("マテリアルスロット:");

        for (size_t i = 0; i < comp->overrideMaterials.size(); ++i)
        {
            MaterialHandle matHandle = comp->overrideMaterials[i];
            if (!matHandle.IsValid()) continue;
            MaterialData *matPtr = rm.GetMaterial(matHandle);
            if (!matPtr) continue;

            ImGui::PushID((int)i);

            char nodeName[64];
            sprintf_s(nodeName, "Material [%zu]", i);

            if (ImGui::TreeNode(nodeName))
            {
                // --- アセット参照状態（このスロットがどの .mat.json 由来か）---
                const std::string &srcPath = (i < comp->overridePaths.size()) ? comp->overridePaths[i] : std::string();
                if (!srcPath.empty())
                    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "アセット: %s", srcPath.c_str());
                else
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "アセット未保存（保存しないとシーン再読込で失われます）");

                if (ImGui::Button(".mat.jsonで保存"))
                {
                    if (auto path = FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle()))
                    {
                        if (rm.SaveMaterialAsset(matHandle, path->string()))
                        {
                            // 保存したパスをアセット参照として記録→シーン保存で永続化される
                            comp->SetData(i, matHandle, path->string());
                            changed = true;
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(".mat.jsonを読込"))
                {
                    if (auto path = FileDialog::OpenPreset("Material", "マテリアルを選択", Graphics::Instance().GetWindowHandle()))
                    {
                        MaterialHandle newHandle = rm.LoadMaterialAsset(path->string());
                        if (newHandle.IsValid())
                        {
                            comp->SetData(i, newHandle, path->string());
                            changed = true;
                        }
                    }
                }
                ImGui::Separator();

                // --- シェーダー選択 ---
                ShaderList sl = GatherShaders(matPtr->shaderHash);
                int        cur = sl.current < 0 ? 0 : sl.current;
                if (!sl.names.empty() &&
                    ImGui::Combo("シェーダー", &cur, sl.names.data(), (int)sl.names.size()))
                {
                    matPtr->shaderHash = ShaderRegistry::Instance().GetRegisteredNames()[cur].second;
                    changed            = true;
                }

                // --- テクスチャ ---
                if (ImGui::TreeNode("テクスチャ"))
                {
                    const char *slotNames[] = {"DiffuseMap", "NormalMap", "EmissiveMap", "RampTexture"};

                    for (const char *slotName : slotNames)
                    {
                        uint32_t      slotHash = CCL::Utils::HashString(slotName);
                        TextureHandle texHandle;
                        bool          hasTexture = matPtr->GetTexture(slotHash, texHandle) && texHandle.IsValid();

                        ImGui::PushID(slotName);
                        ImGui::Text("%s: %s", slotName, hasTexture ? "[Set]" : "[Empty]");
                        ImGui::SameLine();

                        if (ImGui::Button("読込..."))
                        {
                            if (auto path = FileDialog::OpenPreset("Texture", "Select Texture", Graphics::Instance().GetWindowHandle()))
                            {
                                const auto s = path->string();
                                matPtr->SetTexture(slotHash, rm.LoadTexture(s.c_str()), s);
                                changed = true;
                            }
                        }
                        if (!hasTexture && std::string(slotName) == "DiffuseMap")
                        {
                            ImGui::SameLine();
                            if (ImGui::Button("白を設定"))
                            {
                                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whiteTex;
                                GpuResourceUtils::CreateDummyTexture(Graphics::Instance().GetDevice(), 0xFFFFFFFF, whiteTex.GetAddressOf());
                                matPtr->SetTexture(slotHash, rm.RegisterTexture(whiteTex));
                                changed = true;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }

                // --- 詳細パラメータ（シェーダーreflection由来）---
                if (ImGui::TreeNode("詳細パラメータ"))
                {
                    Shader *shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader)
                    {
                        const auto &cbs = shader->GetReflection().GetConstantBuffers();

                        if (ImGui::Button("未設定を既定値で埋める"))
                        {
                            for (const auto &cbInfo : cbs)
                                for (const auto &varInfo : cbInfo.variables)
                                {
                                    if (varInfo.type != D3D_SVT_FLOAT) continue;
                                    const bool isColor  = (varInfo.rows == 1 && varInfo.columns == 4);
                                    const bool isScalar = (varInfo.rows == 1 && varInfo.columns == 1);
                                    if (isColor)
                                    {
                                        DirectX::XMFLOAT4 tmp;
                                        if (!matPtr->GetColor(varInfo.nameHash, tmp))
                                            matPtr->SetColor(varInfo.nameHash, {1.0f, 1.0f, 1.0f, 1.0f});
                                    }
                                    else if (isScalar)
                                    {
                                        float tmp;
                                        if (!matPtr->GetScalar(varInfo.nameHash, tmp))
                                            matPtr->SetScalar(varInfo.nameHash, 1.0f);
                                    }
                                }
                            changed = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("(?)");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "未設定のスカラー/カラーに既定値(1.0 / 白)を入れます。\n"
                                "追加後は下のスライダーで実際の値に調整してください。\n"
                                "テクスチャ有無フラグ(useXXXMap等)も1になるため、\n"
                                "対応するテクスチャが未設定だと黒くなる点に注意。");
                        }
                        ImGui::Separator();

                        for (const auto &cbInfo : cbs)
                        {
                            for (const auto &varInfo : cbInfo.variables)
                            {
                                if (varInfo.type != D3D_SVT_FLOAT) continue;
                                const bool isColor  = (varInfo.rows == 1 && varInfo.columns == 4);
                                const bool isScalar = (varInfo.rows == 1 && varInfo.columns == 1);
                                if (!isColor && !isScalar) continue;

                                ImGui::PushID(varInfo.name.c_str());

                                if (isScalar)
                                {
                                    float val = 0.0f;
                                    if (matPtr->GetScalar(varInfo.nameHash, val))
                                    {
                                        if (ImGui::DragFloat(varInfo.name.c_str(), &val, 0.01f))
                                        {
                                            matPtr->SetScalar(varInfo.nameHash, val);
                                            changed = true;
                                        }
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled("%s (unset)", varInfo.name.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("追加"))
                                        {
                                            matPtr->SetScalar(varInfo.nameHash, 1.0f);
                                            changed = true;
                                        }
                                    }
                                }
                                else // isColor
                                {
                                    DirectX::XMFLOAT4 val = {1, 1, 1, 1};
                                    if (matPtr->GetColor(varInfo.nameHash, val))
                                    {
                                        if (ImGui::ColorEdit4(varInfo.name.c_str(), &val.x))
                                        {
                                            matPtr->SetColor(varInfo.nameHash, val);
                                            changed = true;
                                        }
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled("%s (unset)", varInfo.name.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("追加"))
                                        {
                                            matPtr->SetColor(varInfo.nameHash, {1.0f, 1.0f, 1.0f, 1.0f});
                                            changed = true;
                                        }
                                    }
                                }

                                ImGui::PopID();
                            }
                        }
                    }
                    else
                    {
                        ImGui::TextDisabled("シェーダー未読込。");
                    }
                    ImGui::TreePop();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        return changed;
    }
#else
    static bool CustomGui(MaterialComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(MaterialComponent, "Material")
REGISTER_COMPONENT_EDITOR_GUI(MaterialComponent)
