/// @file  ChromaticAberrationConfigComponentMeta.cpp
/// @brief ChromaticAberrationConfigComponent のシリアライズ/インスペクタ定義(パターンC)。
#include "Engine/Graphics/Components/PostProcess/ChromaticAberrationConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson {
    template <>
    struct ComponentSerializer<ChromaticAberrationConfigComponent> {
        static void Save(json& j, const ChromaticAberrationConfigComponent& c, SaveContext* /*ctx*/) {
            j["enable"]   = c.enable;
            j["centerX"]  = c.data.chromaCenter.x;
            j["centerY"]  = c.data.chromaCenter.y;
            j["strength"] = c.data.chromaStrength;
            j["falloff"]  = c.data.chromaFalloff;
        }
        static void Load(const json& j, ChromaticAberrationConfigComponent& c, LoadContext* /*ctx*/) {
            if (j.contains("enable"))   c.enable              = j["enable"].get<bool>();
            if (j.contains("centerX"))  c.data.chromaCenter.x = j["centerX"].get<float>();
            if (j.contains("centerY"))  c.data.chromaCenter.y = j["centerY"].get<float>();
            if (j.contains("strength")) c.data.chromaStrength = j["strength"].get<float>();
            if (j.contains("falloff"))  c.data.chromaFalloff  = j["falloff"].get<float>();
        }
    };
}

template <> struct ComponentMeta<ChromaticAberrationConfigComponent> {
    static constexpr const char* displayName       = "ChromaticAberration";
    static constexpr const char* category          = CCL::Categories::Effect;
    static constexpr int         sortPriority       = 535;
    static constexpr bool        hiddenInInspector  = false;
    static constexpr bool        hasCustomGui       = true;
    static constexpr bool        isSerializable     = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ChromaticAberrationConfigComponent& c,
                          unsigned long long /*entityID*/ = 0, void* /*world*/ = nullptr) {
        namespace mi = ComponentMetaImGui;
        bool changed = false;
        changed |= mi::Checkbox("有効", &c.enable);

        ImGui::BeginDisabled(!c.enable);
        changed |= mi::DragFloat2("中心(UV)", &c.data.chromaCenter.x, 0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat ("強度",     &c.data.chromaStrength, 0.001f, 0.0f, 0.2f);
        changed |= mi::DragFloat ("減衰",     &c.data.chromaFalloff,  0.05f,  0.0f, 8.0f);
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(ChromaticAberrationConfigComponent&, unsigned long long, void*) { return false; }
#endif

    static const std::vector<FieldDescriptor>& Fields() {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(ChromaticAberrationConfigComponent, "ChromaticAberration")
REGISTER_COMPONENT_EDITOR_GUI(ChromaticAberrationConfigComponent)
