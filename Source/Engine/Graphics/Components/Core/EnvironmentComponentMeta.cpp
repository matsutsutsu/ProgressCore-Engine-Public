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
        changed |= ImGui::DragFloat("Exposure", &comp.data.exposure, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Chromatic Aberration", &comp.data.chromaticAberration, 0.001f, -0.1f, 0.1f);
        changed |= ImGui::ColorEdit3("Color Tint", &comp.data.colorTint.x);
        return changed;
    }
#else
    static bool CustomGui(ToneMapConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(data.exposure, "Exposure", 0.05f, 0.0f, 20.0f, "Settings")
            CFIELD_FLOAT_RANGED(data.chromaticAberration, "Chromatic Aberration", 0.001f, -0.1f, 0.1f, "Settings")
            CFIELD(data.colorTint, "Color Tint", "Settings")
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
    CFIELD(enable, "Enable", "Setup")
    CFIELD(data.threshold, "Threshold", "Filter")
    CFIELD(data.softKnee, "Soft Knee", "Filter")
    CFIELD(data.intensity, "Intensity", "Glow")
    CFIELD(data.radius, "Radius", "Glow")
COMPONENT_META_END(BloomConfigComponent)

COMPONENT_META_BEGIN_FULL(DistortionConfigComponent, "DistortionConfig", CCL::Categories::Effect, 520, false)
    CFIELD(enable, "Enable", "Setup")
    CFIELD(data.time, "Time Multiplier", "Animation")
    CFIELD(data.strength, "Distort Strength", "Parameters")
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

        changed |= ImGui::Checkbox("Enable Fog", &comp.enabled);
        changed |= ImGui::ColorEdit4("Base Color", &comp.color.x);

        if (ImGui::CollapsingHeader("Distance & Height", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat("Start Dist", &comp.start, 0.5f);
            changed |= ImGui::DragFloat("End Dist", &comp.end, 0.5f);
            changed |= ImGui::DragFloat("Height Level", &comp.heightStart, 0.1f);
            changed |= ImGui::DragFloat("Height Density", &comp.heightDensity, 0.01f);
        }

        if (ImGui::CollapsingHeader("Noise & Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Noise Texture:");
            ImGui::SameLine();
            if (ImGui::Button("Load...##FogNoise")) {
                if (auto path = FileDialog::OpenPreset("Texture", "Select Texture", Graphics::Instance().GetWindowHandle())) {

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
                if (ImGui::Button("Clear##FogNoise")) {
                    comp.noiseTextureHandle = TextureHandle{};
                    comp.noiseTexturePath[0] = '\0';
                    changed = true;
                }
            }

            changed |= ImGui::DragFloat("NoiseStrength", &comp.noiseStrength, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat("Scale", &comp.noiseScale, 0.001f);
            changed |= ImGui::DragFloat2("Scroll Speed", &comp.noiseSpeed.x, 0.01f);
        }

        if (ImGui::CollapsingHeader("Rim Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::ColorEdit3("Rim Color", &comp.rimColor.x);
            changed |= ImGui::DragFloat("Strength", &comp.rimStrength, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat("Power", &comp.rimPower, 0.1f, 0.1f, 20.0f);
        }

        return changed;
    }
#else
    static bool CustomGui(FogComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(enabled, "Enabled", "Base")
            CFIELD(color, "Color", "Base")
            CFIELD(start, "Start Dist", "Dist")
            CFIELD(end, "End Dist", "Dist")
            CFIELD(heightStart, "Height Start", "Height")
            CFIELD_FLOAT_RANGED(heightDensity, "Height Density", 0.01f, 0.0f, 1.0f, "Height")
            CFIELD_FLOAT_RANGED(noiseStrength, "Noise Strength", 0.01f, 0.0f, 1.0f, "Noise")
            CFIELD(noiseScale, "Noise Scale", "Noise")
            CFIELD(noiseSpeed, "Noise Speed", "Noise")
            CFIELD(rimColor, "Rim Color", "Rim")
            CFIELD_FLOAT_RANGED(rimStrength, "Rim Strength", 0.1f, 0.0f, 10.0f, "Rim")
            CFIELD_FLOAT_RANGED(rimPower, "Rim Power", 0.1f, 0.0f, 20.0f, "Rim")
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
        ImGui::SeparatorText("Shadow Settings");

        changed |= ImGui::DragFloat3("Cascade Splits (Near/Mid/Far)", &c.cascadeSplits.x, 1.0f, 1.0f, 1000.0f);
        changed |= ImGui::DragFloat("Bias", &c.shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f");
        changed |= ImGui::ColorEdit4("Shadow Color", &c.shadowColor.x);

        return changed;
    }
#else
    static bool CustomGui(ShadowMapConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_FLOAT_RANGED(shadowBias, "Shadow Bias", 0.0001f, 0.0f, 0.01f, "Settings")
            CFIELD(shadowColor, "Shadow Color", "Settings")
            CFIELD(cascadeSplits, "Cascade Splits", "Settings")
        };
        return fields;
    }
};
REGISTER_COMPONENT(ShadowMapConfigComponent, "ShadowMapConfig")
REGISTER_COMPONENT_EDITOR_GUI(ShadowMapConfigComponent)