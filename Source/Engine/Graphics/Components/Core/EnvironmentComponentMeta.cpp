#include "Engine/Graphics/Components/Lighting/FogComponent.h"
#include "Engine/Graphics/Components/Lighting/ShadowMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/BloomConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"
#include "Engine/Graphics/Components/PostProcess/DistortionConfigComponent.h"


#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentMeta.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Dialog.h"

#include "Engine/Serialization/Registry/ComponentRegistry_Editor.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// ToneMap Config
// ============================================================================
template <> struct ComponentMeta<ToneMapConfigComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Tone Mapping";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ToneMapConfigComponent& comp, unsigned long long /* entityID */ = 0, void* /* worldPtr */ = nullptr) {
        bool changed = false;
        changed |= ImGui::DragFloat("Exposure", &comp.data.exposure, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("Chromatic Aberration", &comp.data.chromaticAberration, 0.001f, -0.1f, 0.1f);
        changed |= ImGui::ColorEdit3("Color Tint", &comp.data.colorTint.x);
        return changed;
    }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
        META_FIELD_FLOAT(ToneMapConfigComponent, data.exposure, "exposure", "Exposure", 0.05f, 0.0f, 20.0f, "Settings"),
        META_FIELD_FLOAT(ToneMapConfigComponent, data.chromaticAberration,"chromaticAberration", "Chromatic Aberration", 0.001f, -0.1f, 0.1f, "Settings"),
        META_FIELD_FLOAT4(ToneMapConfigComponent, data.colorTint, "colorTint", "Color Tint",0.1f, "Settings") };
        return fields;
    }
};


// -----------------------------------------------------------------------------------
// [2] BloomConfigComponent
// -----------------------------------------------------------------------------------
COMPONENT_META_BEGIN(BloomConfigComponent, "BloomConfig")
CFIELD(enable, "Enable", "Setup")
CFIELD(data.threshold, "Threshold", "Filter")
CFIELD(data.softKnee, "Soft Knee", "Filter")
CFIELD(data.intensity, "Intensity", "Glow")
CFIELD(data.radius, "Radius", "Glow")
COMPONENT_META_END(BloomConfigComponent)

// -----------------------------------------------------------------------------------
// [3] DistortionConfigComponent （★ 新規実装：歪みエフェクト用）
// 入れ子構造体（data.time 等）であっても、C++標準の offsetof に準拠しているため
// ドットラムダ展開形式でそのままシリアライズ対象に指定可能です。
// -----------------------------------------------------------------------------------
COMPONENT_META_BEGIN(DistortionConfigComponent, "DistortionConfig")
CFIELD(enable, "Enable", "Setup")
CFIELD(data.time, "Time Multiplier", "Animation")
CFIELD(data.strength, "Distort Strength", "Parameters")
COMPONENT_META_END(DistortionConfigComponent)

// ============================================================================
// Fog Component
// ============================================================================
template <> struct ComponentMeta<FogComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "Fog Settings";
    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(FogComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;

        changed |= ImGui::Checkbox(u8"Enable Fog (有効)", &comp.enabled);
        changed |= ImGui::ColorEdit4(u8"Base Color", &comp.color.x);

        if (ImGui::CollapsingHeader(u8"Distance & Height (距離・高さ)", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::DragFloat(u8"Start Dist", &comp.start, 0.5f);
            changed |= ImGui::DragFloat(u8"End Dist", &comp.end, 0.5f);
            changed |= ImGui::DragFloat(u8"Height Level (地面高さ)", &comp.heightStart, 0.1f);
            changed |= ImGui::DragFloat(u8"Height Density (濃さ)", &comp.heightDensity, 0.01f);
        }

        if (ImGui::CollapsingHeader(u8"Noise & Animation (揺らぎ)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text(u8"Noise Texture:");
            ImGui::SameLine();
            if (ImGui::Button("Load...##FogNoise")) {
                if (auto path = Engine::FileDialog::OpenPreset("Texture", "Select Texture",
                    Graphics::Instance().GetWindowHandle()))
                {
					const auto& filename = path->string().c_str();
                    strcpy_s(comp.noiseTexturePath, filename);
                    GpuResourceUtils::LoadTexture(Graphics::Instance().GetDevice(),
                        filename,
                        comp.noiseTextureSRV.ReleaseAndGetAddressOf());
                    changed = true;
                }
            }

            if (comp.noiseTextureSRV) {
                ImGui::Image((void*)comp.noiseTextureSRV.Get(), ImVec2(64, 64));
                ImGui::SameLine();
                if (ImGui::Button("Clear##FogNoise")) {
                    comp.noiseTextureSRV.Reset();
                    comp.noiseTexturePath[0] = '\0';
                    changed = true;
                }
            }

            changed |= ImGui::DragFloat(u8"Strength (強度)", &comp.noiseStrength, 0.01f, 0.0f, 1.0f);
            changed |= ImGui::DragFloat(u8"Scale (サイズ)", &comp.noiseScale, 0.001f);
            changed |= ImGui::DragFloat2(u8"Scroll Speed (流速)", &comp.noiseSpeed.x, 0.01f);
        }

        if (ImGui::CollapsingHeader(u8"Rim Light (Atmosphere)", ImGuiTreeNodeFlags_DefaultOpen)) {
            changed |= ImGui::ColorEdit3(u8"Rim Color", &comp.rimColor.x);
            changed |= ImGui::DragFloat(u8"Strength", &comp.rimStrength, 0.1f, 0.0f, 10.0f);
            changed |= ImGui::DragFloat(u8"Power (鋭さ)", &comp.rimPower, 0.1f, 0.1f, 20.0f);
        }

        return changed;
    }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            META_FIELD_BOOL(FogComponent, enabled, "enabled", "Enabled", "Base"),
            META_FIELD_FLOAT4(FogComponent, color, "color", "Color", 0.01f, "Base"),
            META_FIELD_FLOAT(FogComponent, start, "start", "Start Dist", 0.5f, 0.0f, 0.0f, "Dist"),
            META_FIELD_FLOAT(FogComponent, end, "end", "End Dist", 0.5f, 0.0f, 0.0f, "Dist"),
            META_FIELD_FLOAT(FogComponent, heightStart, "heightStart", "Height Start", 0.1f, 0.0f, 0.0f, "Height"),
            META_FIELD_FLOAT(
                FogComponent, heightDensity, "heightDensity", "Height Density", 0.01f, 0.0f, 1.0f, "Height"),
            META_FIELD_FLOAT(
                FogComponent, noiseStrength, "noiseStrength", "Noise Strength", 0.01f, 0.0f, 1.0f, "Noise"),
            META_FIELD_FLOAT(FogComponent, noiseScale, "noiseScale", "Noise Scale", 0.001f, 0.0f, 0.0f, "Noise"),
            META_FIELD_FLOAT2(FogComponent, noiseSpeed, "noiseSpeed", "Noise Speed", 0.01f, "Noise"),
            META_FIELD_FLOAT3(FogComponent, rimColor, "rimColor", "Rim Color", 0.01f, "Rim"),
            META_FIELD_FLOAT(FogComponent, rimStrength, "rimStrength", "Rim Strength", 0.1f, 0.0f, 10.0f, "Rim"),
            META_FIELD_FLOAT(FogComponent, rimPower, "rimPower", "Rim Power", 0.1f, 0.0f, 20.0f, "Rim")
        };
        return fields;
    }
};

// ============================================================================
// ShadowMap Config
// ============================================================================
template <> struct ComponentMeta<ShadowMapConfigComponent> {
    static constexpr bool        registered = true;
    static constexpr const char* displayName = "ShadowMapConfigComponent";

    static constexpr bool        hasCustomGui = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ShadowMapConfigComponent& c, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr)
    {
        bool changed = false;
        ImGui::SeparatorText("Shadow Settings");

        // &c.cascadeSplits.x を渡すことで、XMFLOAT3 を一気に操作できる
        changed |= ImGui::DragFloat3(u8"Cascade Splits (Near/Mid/Far)", &c.cascadeSplits.x, 1.0f, 1.0f, 1000.0f);

        changed |= ImGui::DragFloat(u8"Bias (縞模様対策)", &c.shadowBias, 0.0001f, 0.0f, 0.01f, "%.5f");
        changed |= ImGui::ColorEdit4(u8"Shadow Color", &c.shadowColor.x);
        return changed;
    }
#endif

    static const std::vector<FieldDescriptor>& Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
           META_FIELD_FLOAT(ShadowMapConfigComponent, shadowBias, "shadowBias", "Shadow Bias", 0.0001f, 0.0f, 0.01f, "Settings"),
            META_FIELD_FLOAT4(ShadowMapConfigComponent, shadowColor, "shadowColor", "Shadow Color", 0.01f, "Settings"),

            // さきほど定義したマクロを使って JSON の保存対象に組み込む
            META_FIELD_FLOAT3(ShadowMapConfigComponent, cascadeSplits, "cascadeSplits", "Cascade Splits", 1.0f, "Settings")
        };
        return fields;
    }
};

// ============================================================================
// 究極の自動化：これだけでJSONとImGuiの両方に登録される
// ============================================================================
REGISTER_COMPONENT(ToneMapConfigComponent, "ToneMapConfig")
REGISTER_COMPONENT(FogComponent, "Fog")
REGISTER_COMPONENT(ShadowMapConfigComponent, "ShadowMapConfig")

REGISTER_COMPONENT_EDITOR_GUI(ToneMapConfigComponent)
REGISTER_COMPONENT_EDITOR_GUI(FogComponent)
REGISTER_COMPONENT_EDITOR_GUI(ShadowMapConfigComponent)