/// @file  MaterialComponentMeta.cpp
/// @brief MaterialComponent のシリアライズ/インスペクタ定義。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"
#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include <vector> // ★追加: shaderNames を ShaderRegistry から動的に組み立てるため

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <>
struct ComponentMeta<MaterialComponent>
{
    static constexpr const char *displayName    = "Material";
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(MaterialComponent &compRef, unsigned long long entityID = 0, void *worldPtr = nullptr)
    {
        bool               changed = false;
        MaterialComponent *comp    = &compRef;
        auto              *world   = static_cast<CCL::ECS::Core::World *>(worldPtr);

        if (comp->overrideMaterials.empty())
        {
            ImGui::TextDisabled("Using Shared Materials (Default)");
            bool hasModel = false;
            if (world)
            {
                auto *modelComp = world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
                if (modelComp && modelComp->GetModel())
                {
                    hasModel = true;
                    if (ImGui::Button("Override All Materials (Edit)"))
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
            if (!hasModel && ImGui::Button("Create Empty Material List"))
            {
                MaterialHandle emptyHandle = ResourceManager::Instance().CreateMaterial();
                comp->overrideMaterials.resize(1, emptyHandle);
                changed = true;
            }
            return changed;
        }

        if (ImGui::Button("Reset to Default (Discard Changes)"))
        {
            for (auto handle : comp->overrideMaterials)
            {
                ResourceManager::Instance().UnloadMaterial(handle);
            }
            comp->overrideMaterials.clear();
            return true;
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Batch Operations (Apply to All)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // ★変更: 手書き配列を廃止し、ShaderRegistry から動的に取得する。
            //         shaders.json に1行足すだけで、ここにも自動で出てくる。
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

                if (ImGui::Button("Apply Shader to All Slots"))
                {
                    // ★変更: 文字列から再ハッシュせず、登録済みのハッシュをそのまま使う
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
                ImGui::TextDisabled("No shaders registered yet (ShaderManifest not loaded?)");
            }
        }

        ImGui::Separator();
        ImGui::Text("Material Slots:");

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
                if (ImGui::Button("Save as .mat.json"))
                {
                    if (auto path = FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle()))
                    {
                        ResourceManager::Instance().SaveMaterialAsset(matHandle, path->string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load .mat.json"))
                {
                    if (auto path = FileDialog::OpenPreset("Material", "Select Material", Graphics::Instance().GetWindowHandle()))
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
                    if (ImGui::ColorEdit4("Main Color", &mainColor.x))
                    {
                        matPtr->SetColor("materialColor"_hash, mainColor);
                        changed = true;
                    }
                }
                else if (ImGui::Button("Add Color Property"))
                {
                    matPtr->SetColor("materialColor"_hash, {1, 1, 1, 1});
                    changed = true;
                }

                // ★変更: 手書き配列(しかも上のBatch用と個数/内容が食い違っていた)を廃止し、
                //         ShaderRegistry から動的に取得する。
                //         現在のシェーダーがどれかも、文字列の再ハッシュではなく
                //         登録済みハッシュとの直接比較で探す（typoに強い）。
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
                if (currentShaderIdx < 0) currentShaderIdx = 0; // 未登録ハッシュ(typo等)は先頭を暫定表示

                if (!shaderNames.empty() &&
                    ImGui::Combo("Shader", &currentShaderIdx, shaderNames.data(), (int)shaderNames.size()))
                {
                    matPtr->shaderHash = registeredShaders[currentShaderIdx].second;
                    changed            = true;
                }

                if (ImGui::TreeNode("Textures"))
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

                        if (ImGui::Button("Load..."))
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
                            if (ImGui::Button("Set White"))
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

                if (ImGui::TreeNode("Advanced Parameters"))
                {
                    Shader *shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader)
                    {
                        const auto &cbs = shader->GetReflection().GetConstantBuffers();

                        // ★追加: 未設定の変数を一括で既定値追加するボタン。
                        //   シェーダーを切り替えた直後、cbufferがゼロ初期化のまま
                        //   (AutoConstantBuffer::Create の cpuBuffer.resize(size, 0))
                        //   だと Glow のように「真っ黒=何も見えない」事故が起きるため、
                        //   スライダーが出る前に明示的に値を持たせられるようにする。
                        if (ImGui::Button("Fill All Missing With Defaults"))
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

                                // ★修正: 以前は else if で columns==4 判定が
                                //   到達不能だった(float4もscalar扱いされていた)ため、
                                //   先に「色か/スカラーか」を確定させてから分岐する。
                                const bool isColor  = (varInfo.rows == 1 && varInfo.columns == 4);
                                const bool isScalar = (varInfo.rows == 1 && varInfo.columns == 1);
                                if (!isColor && !isScalar) continue; // 行列等はこのUIでは非対応

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
                                        // ★追加: 未設定変数はスライダーの代わりに Add ボタンを出す。
                                        //   これが無いと GetScalar==false のままスライダーが
                                        //   一切表示されず、GUIから初期化する手段が無かった。
                                        ImGui::TextDisabled("%s (unset)", varInfo.name.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("Add"))
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
                                        if (ImGui::Button("Add"))
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
                        ImGui::TextDisabled("Shader not loaded.");
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