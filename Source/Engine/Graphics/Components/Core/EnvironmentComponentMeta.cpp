/// @file  EnvironmentComponentMeta.cpp
/// @brief 環境設定関連コンポーネントのシリアライズ/インスペクタ定義。

#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Components/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DistortionConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// ToneMap Config 
// ============================================================================
template <> struct ComponentMeta<ToneMapConfigComponent> {
    CCL_META_USING_THIS(ToneMapConfigComponent)
    static constexpr const char* displayName    = "Tone Mapping";
    static constexpr const char* category       = CCL::Categories::Effect; // ★修正
    static constexpr int         sortPriority   = 500;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ToneMapConfigComponent& comp, unsigned long long /* entityID */ = 0, void* /* worldPtr */ = nullptr) {
        bool changed = false;
        changed |= ImGui::DragFloat("露出", &comp.data.exposure, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("色収差", &comp.data.chromaticAberration, 0.001f, -0.1f, 0.1f);
        changed |= ImGui::ColorEdit3("色補正(Tint)", &comp.data.colorTint.x);
        return changed;
    }
#else
    static bool CustomGui(ToneMapConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(data.exposure, "露出", 0.05f, 0.0f, 20.0f, "Settings")
            CFIELD_FLOAT_RANGED(data.chromaticAberration, "色収差", 0.001f, -0.1f, 0.1f, "Settings")
            CFIELD(data.colorTint, "色補正(Tint)", "Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(ToneMapConfigComponent, "ToneMapConfig")
REGISTER_COMPONENT_EDITOR_GUI(ToneMapConfigComponent)

// ============================================================================
// Bloom / Distortion 
// ★修正: BEGIN_FULL を使用してエフェクトとして明示的にカテゴリ化
// ============================================================================
COMPONENT_META_BEGIN_FULL(BloomConfigComponent, "BloomConfig", CCL::Categories::Effect, 510, false)
    CFIELD(enable, "有効", "Setup")
    CFIELD(data.threshold, "閾値", "Filter")
    CFIELD(data.softKnee, "ソフトニー", "Filter")
    CFIELD(data.intensity, "強度", "Glow")
    CFIELD(data.radius, "半径", "Glow")
COMPONENT_META_END(BloomConfigComponent)

COMPONENT_META_BEGIN_FULL(DistortionConfigComponent, "DistortionConfig", CCL::Categories::Effect, 520, false)
    CFIELD(enable, "有効", "Setup")
    CFIELD(data.time, "時間倍率", "Animation")
    CFIELD(data.strength, "歪み強度", "Parameters")
COMPONENT_META_END(DistortionConfigComponent)

// ============================================================================
// Fog Component 
// ============================================================================
template <> struct ComponentMeta<FogComponent> {
    CCL_META_USING_THIS(FogComponent)
    static constexpr const char* displayName    = "Fog Settings";
    static constexpr const char* category       = CCL::Categories::Rendering; // ★修正
    static constexpr int         sortPriority   = 400;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(FogComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;

        changed |= ImGui::Checkbox("フォグ有効", &comp.enabled);
        changed |= ImGui::ColorEdit4("基本色", &comp.color.x);

        if (ImGui::CollapsingHeader("距離と高さ", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("開始距離", &comp.start, 0.5f);
            changed |= ImGui::DragFloat("終了距離", &comp.end, 0.5f);
            changed |= ImGui::DragFloat("高さ基準", &comp.heightStart, 0.1f);
            changed |= ImGui::DragFloat("高さ密度", &comp.heightDensity, 0.01f);
        }

        if (ImGui::CollapsingHeader("ノイズとアニメーション", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("ノイズテクスチャ:");
            ImGui::SameLine();
            if (ImGui::Button("読込...##FogNoise")) {
                if (auto path = FileDialog::OpenPreset("Texture", "テクスチャを選択", Graphics::Instance().GetWindowHandle())) {

                    // ★修正: Engine:: プレフィックスを削除
                    std::string relativePath = FileDialog::ConvertToRelativePath(*path);

                    strcpy_s(comp.noiseTexturePath, relativePath.c_str());
                    comp.noiseTextureHandle = TextureHandle{}; 
                    changed = true;
                }
            }

            ID3D11ShaderResourceView* previewSRV = nullptr;
            if (comp.noiseTextureHandle.IsValid()) {
                previewSRV = ResourceManager::Instance().GetTexture(comp.noiseTextureHandle);
            }

            if (previewSRV) {
                ImGui::Image((void*)previewSRV, ImVec2(64, 64));
                ImGui::SameLine();
                if (ImGui::Button("クリア##FogNoise")) {
                    comp.noiseTextureHandle = TextureHandle{};
                    comp.noiseTexturePath[0] = '\0';
                    changed = true;
                }
            }

            changed |= ImGui::DragFloat("ノイズ強度", &comp.noiseStrength, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("スケール", &comp.noiseScale, 0.001f);
            changed |= ImGui::DragFloat2("スクロール速度", &comp.noiseSpeed.x, 0.01f);
        }

        if (ImGui::CollapsingHeader("リムライト", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::ColorEdit3("リム色", &comp.rimColor.x);
            changed |= ImGui::DragFloat("強さ", &comp.rimStrength, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat("強度(Power)", &comp.rimPower, 0.1f, 0.1f, 20.0f);
        }

        return changed;
    }
#else
    static bool CustomGui(FogComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(enabled, "有効", "Base")
            CFIELD(color, "色", "Base")
            CFIELD(start, "開始距離", "Dist")
            CFIELD(end, "終了距離", "Dist")
            CFIELD(heightStart, "高さ開始", "Height")
            CFIELD_FLOAT_RANGED(heightDensity, "高さ密度", 0.01f, 0.0f, 1.0f, "Height")
            CFIELD_FLOAT_RANGED(noiseStrength, "ノイズ強度", 0.01f, 0.0f, 1.0f, "Noise")
            CFIELD(noiseScale, "ノイズスケール", "Noise")
            CFIELD(noiseSpeed, "ノイズ速度", "Noise")
            CFIELD(rimColor, "リム色", "Rim")
            CFIELD_FLOAT_RANGED(rimStrength, "リム強度", 0.1f, 0.0f, 10.0f, "Rim")
            CFIELD_FLOAT_RANGED(rimPower, "リム強度(Power)", 0.1f, 0.0f, 20.0f, "Rim")
        };
        return fields;
    }
};
REGISTER_COMPONENT(FogComponent, "Fog")
REGISTER_COMPONENT_EDITOR_GUI(FogComponent)

// ============================================================================
// ShadowMap Config 
// ============================================================================
template <> struct ComponentMeta<ShadowMapConfigComponent> {
    CCL_META_USING_THIS(ShadowMapConfigComponent)
    static constexpr const char* displayName    = "ShadowMap Config";
    static constexpr const char* category       = CCL::Categories::Rendering; // ★修正
    static constexpr int         sortPriority   = 410;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ShadowMapConfigComponent& c, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;
        ImGui::SeparatorText("シャドウ設定");

        changed |= ImGui::DragFloat3("カスケード分割(近/中/遠)", &c.cascadeSplits.x, 1.0f, 1.0f, 1000.0f);
        changed |= ImGui::DragFloat("バイアス", &c.shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f");
        changed |= ImGui::ColorEdit4("シャドウ色", &c.shadowColor.x);

        return changed;
    }
#else
    static bool CustomGui(ShadowMapConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(shadowBias, "シャドウバイアス", 0.0001f, 0.0f, 0.01f, "Settings")
            CFIELD(shadowColor, "シャドウ色", "Settings")
            CFIELD(cascadeSplits, "カスケード分割", "Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(ShadowMapConfigComponent, "ShadowMapConfig")
REGISTER_COMPONENT_EDITOR_GUI(ShadowMapConfigComponent)