#include "Engine/Graphics/Components/PostProcess/DepthOfFieldConfigComponent.h"
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
    struct ComponentSerializer<DepthOfFieldConfigComponent>
    {
        static void Save(json &j, const DepthOfFieldConfigComponent &c, SaveContext * /*ctx*/)
        {
            j["enable"]        = c.enable;
            j["focusDistance"] = c.data.dofFocusDistance;
            j["focusRange"]    = c.data.dofFocusRange;
            j["falloff"]       = c.data.dofFalloff;
            j["maxBlur"]       = c.data.dofMaxBlur;
        }
        static void Load(const json &j, DepthOfFieldConfigComponent &c, LoadContext * /*ctx*/)
        {
            if (j.contains("enable")) c.enable = j["enable"].get<bool>();
            if (j.contains("focusDistance")) c.data.dofFocusDistance = j["focusDistance"].get<float>();
            if (j.contains("focusRange")) c.data.dofFocusRange = j["focusRange"].get<float>();
            if (j.contains("falloff")) c.data.dofFalloff = j["falloff"].get<float>();
            if (j.contains("maxBlur")) c.data.dofMaxBlur = j["maxBlur"].get<float>();
        }
    };
}

template <>
struct ComponentMeta<DepthOfFieldConfigComponent>
{
    static constexpr const char *displayName       = "DepthOfField";
    static constexpr const char *category          = CCL::Categories::Effect;
    static constexpr int         sortPriority      = 528;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(DepthOfFieldConfigComponent &c,
                          unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        namespace mi = ComponentMetaImGui;
        bool changed = false;
        changed |= mi::Checkbox("有効", &c.enable);

        ImGui::BeginDisabled(!c.enable);
        changed |= mi::DragFloat("フォーカス距離", &c.data.dofFocusDistance, 0.1f, 0.0f, 500.0f);
        changed |= mi::DragFloat("フォーカス範囲", &c.data.dofFocusRange, 0.1f, 0.0f, 100.0f);
        changed |= mi::DragFloat("減衰", &c.data.dofFalloff, 0.1f, 0.1f, 100.0f);
        changed |= mi::DragFloat("最大ブラー", &c.data.dofMaxBlur, 0.001f, 0.0f, 0.05f);
        ImGui::EndDisabled();

        return changed;
    }
#else
    static bool CustomGui(DepthOfFieldConfigComponent &, unsigned long long, void *) { return false; }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(DepthOfFieldConfigComponent, "DepthOfField")
REGISTER_COMPONENT_EDITOR_GUI(DepthOfFieldConfigComponent)
