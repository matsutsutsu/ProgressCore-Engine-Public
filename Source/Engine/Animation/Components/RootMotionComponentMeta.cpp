#include "RootMotionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson
{
    // 中身は実行時の状態なので、空の枠だけを保存・読込する
    template <>
    struct ComponentSerializer<RootMotionComponent>
    {
        static void Save(json & /*j*/, const RootMotionComponent & /*c*/, SaveContext * /*ctx*/)
        {
        }
        static void Load(const json & /*j*/, RootMotionComponent & /*c*/, LoadContext * /*ctx*/)
        {
        }
    };
}



template <>
struct ComponentMeta<RootMotionComponent>
{
    CCL_META_USING_THIS(RootMotionComponent)

    static constexpr const char *displayName       = "Root Motion";
    static constexpr const char *category          = CCL::Categories::Animation;
    static constexpr int         sortPriority      = 110;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(RootMotionComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        // 中身は RootMotionSystem が毎ステップ書くので、確認用に 1 行だけ出す
        ImGui::TextDisabled("%s / 前回 %.2f 秒%s", comp.enabled ? "適用中" : "適用なし", comp.prevTimer,
                            comp.externallyDriven ? " / 外部が移動中" : "");
        return false;
    }
#else
    static bool CustomGui(RootMotionComponent &, unsigned long long, void *)
    {
        return false;
    }
#endif

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> empty;
        return empty;
    }
};

REGISTER_COMPONENT(RootMotionComponent, "RootMotion")
REGISTER_COMPONENT_EDITOR_GUI(RootMotionComponent)
