/// @file  LightComponentMeta.cpp
/// @brief ライト関連コンポーネント (Directional, Point, Spot) のシリアライズ/インスペクタ定義。

#include "Engine/Graphics/Components/Lighting/LightComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// ============================================================================
// Directional Light
// ============================================================================
template <> struct ComponentMeta<DirectionalLightComponent> {
    CCL_META_USING_THIS(DirectionalLightComponent)

    static constexpr const char* displayName    = "DirectionalLight";
    static constexpr const char* category       = CCL::Categories::Light; // ★追加
    static constexpr int         sortPriority   = 300; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(DirectionalLightComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;
        changed |= ImGui::ColorEdit3("ライト色", &comp.color.x);
        changed |= ImGui::DragFloat("強度", &comp.intensity, 0.05f, 0.0f, 100.0f);
        ImGui::Separator();
        changed |= ImGui::ColorEdit4("空の色", &comp.skyColor.x);
        changed |= ImGui::ColorEdit4("地面の色", &comp.groundColor.x);
        return changed;
    }
#else
    static bool CustomGui(DirectionalLightComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(color,       "色",        "Lighting")
            CFIELD(intensity,   "強度",    "Lighting")
            CFIELD(skyColor,    "空の色",    "Hemisphere")
            CFIELD(groundColor, "地面の色", "Hemisphere")
        };
        return fields;
    }
};

// ============================================================================
// Point Light
// ============================================================================
template <> struct ComponentMeta<PointLightComponent> {
    CCL_META_USING_THIS(PointLightComponent)

    static constexpr const char* displayName    = "PointLight";
    static constexpr const char* category       = CCL::Categories::Light; // ★追加
    static constexpr int         sortPriority   = 310; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(PointLightComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;
        changed |= ImGui::ColorEdit3("ライト色", &comp.color.x);
        changed |= ImGui::DragFloat("強度", &comp.intensity, 0.05f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("範囲", &comp.range, 0.5f, 0.0f, 1000.0f);
        ImGui::Separator();
        ImGui::Text("Attenuation (減衰パラメータ)");
        changed |= ImGui::DragFloat("定数減衰", &comp.constant, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("線形減衰", &comp.linear, 0.01f, 0.0f, 10.0f);
        changed |= ImGui::DragFloat("二次減衰", &comp.quadratic, 0.001f, 0.0f, 1.0f);
        return changed;
    }
#else
    static bool CustomGui(PointLightComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(color,      "色",     "Lighting")
            CFIELD(intensity,  "強度", "Lighting")
            CFIELD(range,      "範囲",     "Lighting")
            CFIELD(constant,   "定数減衰",  "Attenuation")
            CFIELD(linear,     "線形減衰",    "Attenuation")
            CFIELD(quadratic,  "二次減衰", "Attenuation")
        };
        return fields;
    }
};

// ============================================================================
// Spot Light
// ============================================================================
template <> struct ComponentMeta<SpotLightComponent> {
    CCL_META_USING_THIS(SpotLightComponent)

    static constexpr const char* displayName    = "SpotLight";
    static constexpr const char* category       = CCL::Categories::Light; // ★追加
    static constexpr int         sortPriority   = 320; // ★追加
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui   = true;
    static constexpr bool        isSerializable = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(SpotLightComponent& comp, unsigned long long /*entityID*/ = 0, void* /*worldPtr*/ = nullptr) {
        bool changed = false;
        changed |= ImGui::ColorEdit3("ライト色", &comp.color.x);
        changed |= ImGui::DragFloat("強度", &comp.intensity, 0.05f, 0.0f, 100.0f);
        changed |= ImGui::DragFloat("範囲", &comp.range, 0.5f, 0.0f, 1000.0f);
        ImGui::Separator();
        ImGui::Text("コーン角度(Cos値)");
        changed |= ImGui::SliderFloat("内側コーン(Cos)", &comp.innerCos, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("外側コーン(Cos)", &comp.outerCos, 0.0f, 1.0f);
        if (comp.innerCos < comp.outerCos) { comp.innerCos = comp.outerCos; changed = true; }
        return changed;
    }
#else
    static bool CustomGui(SpotLightComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD(color,     "色",             "Lighting")
            CFIELD(intensity, "強度",         "Lighting")
            CFIELD(range,     "範囲",             "Lighting")
            CFIELD(innerCos,  "内側コーン(Cos)",  "Cone")
            CFIELD(outerCos,  "外側コーン(Cos)",  "Cone")
        };
        return fields;
    }
};

REGISTER_COMPONENT(DirectionalLightComponent, "DirectionalLight")
REGISTER_COMPONENT_EDITOR_GUI(DirectionalLightComponent)

REGISTER_COMPONENT(PointLightComponent, "PointLight")
REGISTER_COMPONENT_EDITOR_GUI(PointLightComponent)

REGISTER_COMPONENT(SpotLightComponent, "SpotLight")
REGISTER_COMPONENT_EDITOR_GUI(SpotLightComponent)