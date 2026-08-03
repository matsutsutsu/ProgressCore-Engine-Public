/// @file  MaterialComponentMeta.cpp
/// @brief MaterialComponent のシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
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
    static bool CustomGui(MaterialComponent &compRef, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool               changed = false;
        MaterialComponent *comp    = &compRef;
        auto              *world   = static_cast<CCL::ECS::World *>(worldPtr);

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
                        comp->overrideMaterials.resize(meshes.size());
                        for (size_t i = 0; i < meshes.size(); ++i)
                        {
                            auto          *originalMat = meshes[i].material;
                            MaterialHandle newHandle   = ResourceManager::Instance().CreateMaterial();
                            MaterialData  *newData     = ResourceManager::Instance().GetMaterial(newHandle);
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
                MaterialHandle emptyHandle = ResourceManager::Instance().CreateMaterial();
                comp->overrideMaterials.resize(1, emptyHandle);
                changed = true;
            }
            return changed;
        }

        if (ImGui::Button("既定に戻す(変更を破棄)"))
        {
            for (auto handle : comp->overrideMaterials)
            {
                ResourceManager::Instance().UnloadMaterial(handle);
            }
            comp->overrideMaterials.clear();
            return true;
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("一括操作(全スロットに適用)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const auto               &registeredShaders = ShaderRegistry::Instance().GetRegisteredNames();
            std::vector<const char *> shaderNames;
            shaderNames.reserve(registeredShaders.size());
            for (const auto &entry : registeredShaders)
                shaderNames.push_back(entry.first.c_str());

            static int batchShaderIdx = 0;

            if (!shaderNames.empty())
            {
                if (batchShaderIdx >= (int)shaderNames.size()) batchShaderIdx = 0;

                ImGui::SetNextItemWidth(150.0f);
                ImGui::Combo("##BatchShader", &batchShaderIdx, shaderNames.data(), (int)shaderNames.size());
                ImGui::SameLine();

                if (ImGui::Button("全スロットにシェーダー適用"))
                {
                    uint32_t targetHash = registeredShaders[batchShaderIdx].second;
                    for (size_t i = 0; i < comp->overrideMaterials.size(); ++i)
                    {
                        MaterialHandle handle = comp->overrideMaterials[i];
                        if (!handle.IsValid()) continue;
                        MaterialData *matPtr = ResourceManager::Instance().GetMaterial(handle);
                        if (matPtr)
                        {
                            matPtr->shaderHash = targetHash;
                        }
                    }
                    changed = true;
                }
            }
            else
            {
                ImGui::TextDisabled("シェーダー未登録(ShaderManifest未読込?)");
            }
        }

        ImGui::Separator();
        ImGui::Text("マテリアルスロット:");

        for (size_t i = 0; i < comp->overrideMaterials.size(); ++i)
        {
            MaterialHandle matHandle = comp->overrideMaterials[i];
            if (!matHandle.IsValid()) continue;
            MaterialData *matPtr = ResourceManager::Instance().GetMaterial(matHandle);
            if (!matPtr) continue;

            ImGui::PushID((int)i);

            char nodeName[64];
            sprintf_s(nodeName, "Material [%zu]", i);

            if (ImGui::TreeNode(nodeName))
            {
                if (ImGui::Button(".mat.jsonで保存"))
                {
                    if (auto path = FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle()))
                    {
                        ResourceManager::Instance().SaveMaterialAsset(matHandle, path->string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button(".mat.jsonを読込"))
                {
                    if (auto path = FileDialog::OpenPreset("Material", "マテリアルを選択", Graphics::Instance().GetWindowHandle()))
                    {
                        MaterialHandle newHandle = ResourceManager::Instance().LoadMaterialAsset(path->string());
                        if (newHandle.IsValid())
                        {
                            comp->SetData(i, newHandle);
                            changed = true;
                        }
                    }
                }
                ImGui::Separator();

                DirectX::XMFLOAT4 mainColor;
                if (matPtr->GetColor("materialColor"_hash, mainColor))
                {
                    if (ImGui::ColorEdit4("メインカラー", &mainColor.x))
                    {
                        matPtr->SetColor("materialColor"_hash, mainColor);
                        changed = true;
                    }
                }
                else if (ImGui::Button("カラープロパティ追加"))
                {
                    matPtr->SetColor("materialColor"_hash, {1, 1, 1, 1});
                    changed = true;
                }

                const auto               &registeredShaders = ShaderRegistry::Instance().GetRegisteredNames();
                std::vector<const char *> shaderNames;
                shaderNames.reserve(registeredShaders.size());
                int currentShaderIdx = -1;
                for (size_t s = 0; s < registeredShaders.size(); ++s)
                {
                    shaderNames.push_back(registeredShaders[s].first.c_str());
                    if (registeredShaders[s].second == matPtr->shaderHash)
                    {
                        currentShaderIdx = (int)s;
                    }
                }
                if (currentShaderIdx < 0) currentShaderIdx = 0; 

                if (!shaderNames.empty() &&
                    ImGui::Combo("シェーダー", &currentShaderIdx, shaderNames.data(), (int)shaderNames.size()))
                {
                    matPtr->shaderHash = registeredShaders[currentShaderIdx].second;
                    changed            = true;
                }

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
                                matPtr->SetTexture(slotHash, ResourceManager::Instance().LoadTexture(s.c_str()), s);
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
                                matPtr->SetTexture(slotHash, ResourceManager::Instance().RegisterTexture(whiteTex));
                                changed = true;
                            }
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNode("詳細パラメータ"))
                {
                    Shader *shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader)
                    {
                        const auto &cbs = shader->GetReflection().GetConstantBuffers();

                        if (ImGui::Button("未設定を既定値で埋める"))
                        {
                            for (const auto &cbInfo : cbs)
                            {
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
                                else if (isColor)
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