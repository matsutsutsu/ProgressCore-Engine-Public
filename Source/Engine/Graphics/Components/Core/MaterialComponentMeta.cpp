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

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <> struct ComponentMeta<MaterialComponent> {
    static constexpr const char* displayName = "Material";
    static constexpr bool        hasCustomGui = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(MaterialComponent& compRef, unsigned long long entityID = 0, void* worldPtr = nullptr) {
        bool changed = false;
        MaterialComponent* comp = &compRef;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);

        if (comp->overrideMaterials.empty()) {
            ImGui::TextDisabled("Using Shared Materials (Default)");
            bool hasModel = false;
            if (world) {
                auto* modelComp = world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
                if (modelComp && modelComp->GetModel()) {
                    hasModel = true;
                    if (ImGui::Button("Override All Materials (Edit)")) {
                        const auto& meshes = modelComp->GetModel()->GetMeshes();
                        comp->overrideMaterials.resize(meshes.size());
                        for (size_t i = 0; i < meshes.size(); ++i) {
                            auto* originalMat = meshes[i].material;
                            MaterialHandle newHandle = ResourceManager::Instance().CreateMaterial();
                            MaterialData* newData = ResourceManager::Instance().GetMaterial(newHandle);
                            if (originalMat && originalMat->data && newData) {
                                *newData = *originalMat->data;
                            }
                            comp->SetData(i, newHandle);
                        }
                        changed = true;
                    }
                }
            }
            if (!hasModel && ImGui::Button("Create Empty Material List")) {
                MaterialHandle emptyHandle = ResourceManager::Instance().CreateMaterial();
                comp->overrideMaterials.resize(1, emptyHandle);
                changed = true;
            }
            return changed;
        }

        if (ImGui::Button("Reset to Default (Discard Changes)")) {
            for (auto handle : comp->overrideMaterials) {
                ResourceManager::Instance().UnloadMaterial(handle);
            }
            comp->overrideMaterials.clear();
            return true;
        }

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Batch Operations (Apply to All)", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shaderNames[] = { "Basic", "Lambert", "Phong", "Toon", "Outline", "ShadowMap", "PBR" };
            static int batchShaderIdx = 6;

            ImGui::SetNextItemWidth(150.0f);
            ImGui::Combo("##BatchShader", &batchShaderIdx, shaderNames, 7);
            ImGui::SameLine();

            if (ImGui::Button("Apply Shader to All Slots")) {
                uint32_t targetHash = CCL::Utils::HashString(shaderNames[batchShaderIdx]);
                for (size_t i = 0; i < comp->overrideMaterials.size(); ++i) {
                    MaterialHandle handle = comp->overrideMaterials[i];
                    if (!handle.IsValid()) continue;
                    MaterialData* matPtr = ResourceManager::Instance().GetMaterial(handle);
                    if (matPtr) {
                        matPtr->shaderHash = targetHash;
                    }
                }
                changed = true;
            }
        }

        ImGui::Separator();
        ImGui::Text("Material Slots:");

        for (size_t i = 0; i < comp->overrideMaterials.size(); ++i) {
            MaterialHandle matHandle = comp->overrideMaterials[i];
            if (!matHandle.IsValid()) continue;
            MaterialData* matPtr = ResourceManager::Instance().GetMaterial(matHandle);
            if (!matPtr) continue;

            ImGui::PushID((int)i);

            char nodeName[64];
            sprintf_s(nodeName, "Material [%zu]", i);

            if (ImGui::TreeNode(nodeName)) {
                if (ImGui::Button("Save as .mat.json")) {
                    if (auto path = FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle())) {
                        ResourceManager::Instance().SaveMaterialAsset(matHandle, path->string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load .mat.json")) {
                    if (auto path = FileDialog::OpenPreset("Material", "Select Material", Graphics::Instance().GetWindowHandle())) {
                        MaterialHandle newHandle = ResourceManager::Instance().LoadMaterialAsset(path->string());
                        if (newHandle.IsValid()) {
                            comp->SetData(i, newHandle);
                            changed = true;
                        }
                    }
                }
                ImGui::Separator();

                DirectX::XMFLOAT4 mainColor;
                if (matPtr->GetColor("materialColor"_hash, mainColor)) {
                    if (ImGui::ColorEdit4("Main Color", &mainColor.x)) {
                        matPtr->SetColor("materialColor"_hash, mainColor);
                        changed = true;
                    }
                }
                else if (ImGui::Button("Add Color Property")) {
                    matPtr->SetColor("materialColor"_hash, { 1, 1, 1, 1 });
                    changed = true;
                }

                const char* shaderNames[] = { "Basic", "Lambert", "Phong", "Toon", "Outline", "ShadowMap", "PBR", "Trail" };
                int currentShaderIdx = 0;
                for (int s = 0; s < 8; ++s) {
                    if (matPtr->shaderHash == CCL::Utils::HashString(shaderNames[s])) {
                        currentShaderIdx = s;
                        break;
                    }
                }
                if (ImGui::Combo("Shader", &currentShaderIdx, shaderNames, 8)) {
                    matPtr->shaderHash = CCL::Utils::HashString(shaderNames[currentShaderIdx]);
                    changed = true;
                }

                if (ImGui::TreeNode("Textures")) {
                    const char* slotNames[] = { "DiffuseMap", "NormalMap", "EmissiveMap", "RampTexture" };

                    for (const char* slotName : slotNames) {
                        uint32_t slotHash = CCL::Utils::HashString(slotName);
                        TextureHandle texHandle;
                        bool hasTexture = matPtr->GetTexture(slotHash, texHandle) && texHandle.IsValid();

                        ImGui::PushID(slotName);
                        ImGui::Text("%s: %s", slotName, hasTexture ? "[Set]" : "[Empty]");
                        ImGui::SameLine();

                        if (ImGui::Button("Load...")) {
                            if (auto path = FileDialog::OpenPreset("Texture", "Select Texture", Graphics::Instance().GetWindowHandle())) {
                                const auto s = path->string();
                                matPtr->SetTexture(slotHash, ResourceManager::Instance().LoadTexture(s.c_str()), s);
                                changed = true;
                            }
                        }
                        if (!hasTexture && std::string(slotName) == "DiffuseMap") {
                            ImGui::SameLine();
                            if (ImGui::Button("Set White")) {
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

                if (ImGui::TreeNode("Advanced Parameters")) {
                    Shader* shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader) {
                        const auto& cbs = shader->GetReflection().GetConstantBuffers();
                        for (const auto& cbInfo : cbs) {
                            for (const auto& varInfo : cbInfo.variables) {
                                if (varInfo.type == D3D_SVT_FLOAT) {
                                    float val = 0.0f;
                                    if (matPtr->GetScalar(varInfo.nameHash, val)) {
                                        if (ImGui::DragFloat(varInfo.name.c_str(), &val, 0.01f)) {
                                            matPtr->SetScalar(varInfo.nameHash, val);
                                            changed = true;
                                        }
                                    }
                                }
                                else if (varInfo.type == D3D_SVT_FLOAT && varInfo.columns == 4) {
                                    DirectX::XMFLOAT4 val = { 1, 1, 1, 1 };
                                    if (matPtr->GetColor(varInfo.nameHash, val)) {
                                        if (ImGui::ColorEdit4(varInfo.name.c_str(), &val.x)) {
                                            matPtr->SetColor(varInfo.nameHash, val);
                                            changed = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else {
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
    static bool CustomGui(MaterialComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(MaterialComponent, "Material")
REGISTER_COMPONENT_EDITOR_GUI(MaterialComponent)