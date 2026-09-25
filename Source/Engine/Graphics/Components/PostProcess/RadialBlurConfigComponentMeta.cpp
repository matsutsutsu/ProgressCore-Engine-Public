// RadialBlurConfigComponent のシリアライズ/インスペクタ定義。
#include "Engine/Graphics/Components/PostProcess/RadialBlurConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson
{
    template <>
    struct ComponentSerializer<RadialBlurConfigComponent>
    {
        static void Save(json &j, const RadialBlurConfigComponent &c, SaveContext * /*ctx*/)
        {
            j["enable"]   = c.enable;
            j["centerX"]  = c.data.blurCenter.x;
            j["centerY"]  = c.data.blurCenter.y;
            j["strength"] = c.data.blurStrength;
            j["falloff"]  = c.data.blurFalloff;
        }
        static void Load(const json &j, RadialBlurConfigComponent &c, LoadContext * /*ctx*/)
        {
            if (j.contains("enable")) c.enable = j["enable"].get<bool>();
            if (j.contains("centerX")) c.data.blurCenter.x = j["centerX"].get<float>();
            if (j.contains("centerY")) c.data.blurCenter.y = j["centerY"].get<float>();
            if (j.contains("strength")) c.data.blurStrength = j["strength"].get<float>();
            if (j.contains("falloff")) c.data.blurFalloff = j["falloff"].get<float>();
        }
    };
} // namespace ComponentMetaJson

template <>
struct ComponentMeta<RadialBlurConfigComponent>
{
    static constexpr const char *displayName       = "RadialBlur";
    static constexpr const char *category          = CCL::Categories::Effect;
    static constexpr int         sortPriority      = 530;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(RadialBlurConfigComponent &c,
                          unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        namespace mi = ComponentMetaImGui;
        bool changed = false;
        changed |= mi::Checkbox("有効", &c.enable);

        ImGui::BeginDisabled(!c.enable);
        changed |= mi::DragFloat2("中心(UV)", &c.data.blurCenter.x, 0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat("強度", &c.data.blurStrength, 0.005f, 0.0f, 1.0f);
        changed |= mi::DragFloat("減衰", &c.data.blurFalloff, 0.05f, 0.0f, 8.0f);
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(RadialBlurConfigComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(RadialBlurConfigComponent, "RadialBlur")
REGISTER_COMPONENT_EDITOR_GUI(RadialBlurConfigComponent)
