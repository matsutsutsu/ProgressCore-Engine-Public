#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Core/MaterialComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Core/Math/StringHash.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"

#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

template <> struct ComponentMeta<MaterialComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Material";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(MaterialComponent& compRef, unsigned long long entityID = 0, void* worldPtr = nullptr)
    {
        bool               changed = false;
        MaterialComponent* comp = &compRef;
        auto* world = static_cast<CCL::ECS::Core::World*>(worldPtr);

        if (comp->overrideMaterials.empty()) {
            ImGui::TextDisabled("Using Shared Materials (Default)");
            bool hasModel = false;
            if (world) {
                auto* modelComp =
                    world->GetComponent<ModelComponent>(static_cast<CCL::ECS::EntityID>(entityID));
                if (modelComp && modelComp->GetModel()) {
                    hasModel = true;
                    if (ImGui::Button("Override All Materials (Edit)")) {
                        const auto& meshes = modelComp->GetModel()->GetMeshes();
                        comp->overrideMaterials.resize(meshes.size());
                        for (size_t i = 0; i < meshes.size(); ++i) {
                            auto* originalMat = meshes[i].material;
                            MaterialHandle newHandle = ResourceManager::Instance().CreateMaterial();
                            MaterialData* newData =
                                ResourceManager::Instance().GetMaterial(newHandle);
                            if (originalMat && originalMat->data && newData) {
                                // ★ データを丸ごとコピー (最速のメモリコピー)
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

        // ====================================================================
        //  一括変更（バッチオペレーション）セクション
        // ====================================================================
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Batch Operations (Apply to All)", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* shaderNames[] = { "Basic", "Lambert", "Phong", "Toon", "Outline", "ShadowMap", "PBR" };
            static int batchShaderIdx = 6; // デフォルトをPBRにしておく等の工夫

            // 少し幅を調整してボタンと並べる
            ImGui::SetNextItemWidth(150.0f);
            ImGui::Combo("##BatchShader", &batchShaderIdx, shaderNames, 7);
            ImGui::SameLine();

            // 一括適用ボタン
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

            // =======================================================
            // ⭕ 修正: 文字列メンバを持たないので、表示用の名前をその場で作る
            // =======================================================
            char nodeName[64];
            sprintf_s(nodeName, "Material [%zu]", i);

            // 作成した動的な文字列を TreeNode に渡す
            if (ImGui::TreeNode(nodeName)) {

                // =========================================================
                // ★追加: マテリアルの保存・ロードボタン
                // =========================================================
                if (ImGui::Button("Save as .mat.json")) {
                    if (auto path = Engine::FileDialog::SavePreset("Material", "Assets/Materials", "mat.json", Graphics::Instance().GetWindowHandle())) {
                        ResourceManager::Instance().SaveMaterialAsset(matHandle, path->string());
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Load .mat.json")) {
                    if (auto path = Engine::FileDialog::OpenPreset("Material", "Select Material", Graphics::Instance().GetWindowHandle())) {
                        MaterialHandle newHandle = ResourceManager::Instance().LoadMaterialAsset(path->string());
                        if (newHandle.IsValid()) {
                            comp->SetData(i, newHandle); // 読み込んだマテリアルに差し替え！
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

                // 文字列ハッシュのリストから選択させる
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
                            if (auto path = Engine::FileDialog::OpenPreset("Texture", "Select Texture", Graphics::Instance().GetWindowHandle()))
                            {
                                const auto s = path->string();
                                // ★修正: テクスチャをセットする際、第3引数にパスを渡して保存対象にする
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

                // =========================================================
                // ⭕ 修正後: シェーダー側の変数リストを基準にしてUIを作る
                // =========================================================
                if (ImGui::TreeNode("Advanced Parameters")) {
                    Shader* shader = ShaderRegistry::Instance().GetShader(matPtr->shaderHash, Graphics::Instance().GetDevice());
                    if (shader) {
                        const auto& cbs = shader->GetReflection().GetConstantBuffers();
                        for (const auto& cbInfo : cbs) {
                            for (const auto& varInfo : cbInfo.variables) {

                                // スカラー値 (float) のUI
                                if (varInfo.type == D3D_SVT_FLOAT) {
                                    float val = 0.0f;
                                    if (matPtr->GetScalar(varInfo.nameHash, val)) {
                                        if (ImGui::DragFloat(varInfo.name.c_str(), &val, 0.01f)) {
                                            matPtr->SetScalar(varInfo.nameHash, val);
                                            changed = true;
                                        }
                                    }
                                }
                                // カラー値 (float4) のUI
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
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

// ============================================================================
// 究極の自動化：これだけでJSONとImGuiの両方に登録される
// ============================================================================
REGISTER_COMPONENT(MaterialComponent, "Material")
REGISTER_COMPONENT_EDITOR_GUI(MaterialComponent)