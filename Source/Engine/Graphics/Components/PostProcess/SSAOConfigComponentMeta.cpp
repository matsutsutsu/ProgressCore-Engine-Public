/// @file  SSAOConfigComponentMeta.cpp
/// @brief SSAOConfigComponent のシリアライズ/インスペクタ定義(パターンC)。
#include "Engine/Graphics/Components/PostProcess/SSAOConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<SSAOConfigComponent> {
        static void Save(json& j, const SSAOConfigComponent& c, SaveContext* /*ctx*/) {
            j["enable"]      = c.enable;
            j["radius"]      = c.data.ssaoRadius;
            j["intensity"]   = c.data.ssaoIntensity;
            j["bias"]        = c.data.ssaoBias;
            j["maxDistance"] = c.data.ssaoMaxDistance;
        }
        static void Load(const json& j, SSAOConfigComponent& c, LoadContext* /*ctx*/) {
            if (j.contains("enable"))      c.enable                  = j["enable"].get<bool>();
            if (j.contains("radius"))      c.data.ssaoRadius         = j["radius"].get<float>();
            if (j.contains("intensity"))   c.data.ssaoIntensity      = j["intensity"].get<float>();
            if (j.contains("bias"))        c.data.ssaoBias           = j["bias"].get<float>();
            if (j.contains("maxDistance")) c.data.ssaoMaxDistance    = j["maxDistance"].get<float>();
        }
    };
}

template <> struct ComponentMeta<SSAOConfigComponent> {
    static constexpr const char* displayName       = "SSAO";
    static constexpr const char* category          = CCL::Categories::Effect;
    static constexpr int         sortPriority       = 520;
    static constexpr bool        hiddenInInspector  = false;
    static constexpr bool        hasCustomGui       = true;
    static constexpr bool        isSerializable     = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(SSAOConfigComponent& c,
                          unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        bool changed = false;
        if (ImGui::Checkbox("Enable", &c.enable)) changed = true;

        ImGui::BeginDisabled(!c.enable);
        if (ImGui::DragFloat("Radius (m)", &c.data.ssaoRadius, 0.01f, 0.05f, 3.0f)) changed = true;
        if (ImGui::DragFloat("Intensity", &c.data.ssaoIntensity, 0.01f, 0.0f, 1.0f)) changed = true;
        if (ImGui::DragFloat("Bias", &c.data.ssaoBias, 0.001f, 0.0f, 0.2f)) changed = true;
        if (ImGui::DragFloat("Max Distance (m)", &c.data.ssaoMaxDistance, 1.0f, 1.0f, 500.0f)) changed = true;

        ImGui::Separator();
        ImGui::TextDisabled("AOは演出ではなく絵の基礎品質。\n薄く常時掛けるのが正しい使い方。");
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(SSAOConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(SSAOConfigComponent, "SSAO")
REGISTER_COMPONENT_EDITOR_GUI(SSAOConfigComponent)
