// ContactShadowConfigComponent のシリアライズ/インスペクタ定義(パターンC)。
#include "Engine/Graphics/Components/PostProcess/ContactShadowConfigComponent.h"
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
    struct ComponentSerializer<ContactShadowConfigComponent>
    {
        static void Save(json &j, const ContactShadowConfigComponent &c, SaveContext * /*ctx*/)
        {
            j["enable"]      = c.enable;
            j["rayLength"]   = c.data.csRayLength;
            j["thickness"]   = c.data.csThickness;
            j["bias"]        = c.data.csBias;
            j["intensity"]   = c.data.csIntensity;
            j["maxDistance"] = c.data.csMaxDistance;
        }
        static void Load(const json &j, ContactShadowConfigComponent &c, LoadContext * /*ctx*/)
        {
            if (j.contains("enable")) c.enable = j["enable"].get<bool>();
            if (j.contains("rayLength")) c.data.csRayLength = j["rayLength"].get<float>();
            if (j.contains("thickness")) c.data.csThickness = j["thickness"].get<float>();
            if (j.contains("bias")) c.data.csBias = j["bias"].get<float>();
            if (j.contains("intensity")) c.data.csIntensity = j["intensity"].get<float>();
            if (j.contains("maxDistance")) c.data.csMaxDistance = j["maxDistance"].get<float>();
        }
    };
} // namespace ComponentMetaJson

template <>
struct ComponentMeta<ContactShadowConfigComponent>
{
    static constexpr const char *displayName       = "ContactShadow";
    static constexpr const char *category          = CCL::Categories::Effect;
    static constexpr int         sortPriority      = 521;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(ContactShadowConfigComponent &c,
                          unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        namespace mi = ComponentMetaImGui;
        bool changed = false;
        changed |= mi::Checkbox("有効", &c.enable);

        ImGui::BeginDisabled(!c.enable);
        changed |= mi::DragFloat("レイ長(m)", &c.data.csRayLength, 0.01f, 0.05f, 2.0f);
        changed |= mi::DragFloat("厚み(m)", &c.data.csThickness, 0.01f, 0.01f, 1.0f);
        changed |= mi::DragFloat("バイアス", &c.data.csBias, 0.001f, 0.0f, 0.2f);
        changed |= mi::DragFloat("強度", &c.data.csIntensity, 0.01f, 0.0f, 1.0f);
        changed |= mi::DragFloat("最大距離(m)", &c.data.csMaxDistance, 1.0f, 1.0f, 200.0f);

        ImGui::Separator();
        ImGui::TextDisabled("光源方向へ深度を辿る影。\nライトを動かすと影の向きも変わる。");
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(ContactShadowConfigComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(ContactShadowConfigComponent, "ContactShadow")
REGISTER_COMPONENT_EDITOR_GUI(ContactShadowConfigComponent)
