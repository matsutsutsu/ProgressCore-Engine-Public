/// @file  RootMotionComponentMeta.cpp
/// @brief RootMotionComponent のメタ定義。
///
/// 設計方針(Why):
///   実行時(Runtime)データであるため、中身はシリアライズさせず
///   パターンC（カスタムシリアライズ）を用いて空枠のみをセーブ・ロードする。

#include "RootMotionComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加
#include "Engine/Serialization/Meta/ComponentMetaJson.h"

#ifdef ENABLE_EDITOR
#include <imgui.h>
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

namespace ComponentMetaJson
{
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
} // namespace ComponentMetaJson

template <>
struct ComponentMeta<RootMotionComponent>
{
    CCL_META_USING_THIS(RootMotionComponent)

    static constexpr const char *displayName       = "Root Motion";
    static constexpr const char *category          = CCL::Categories::Animation; // ★修正: 定数を使用
    static constexpr int         sortPriority      = 110;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = true;
    static constexpr bool        isSerializable    = true;

#ifdef ENABLE_EDITOR
    static bool CustomGui(RootMotionComponent &comp, unsigned long long /*entityID*/ = 0, void * /*world*/ = nullptr)
    {
        ImGui::TextDisabled("[Runtime State: システムによって自動制御されます]");
        ImGui::Separator();

        ImGui::BeginDisabled();
        ImGui::Checkbox("Enabled", &comp.enabled);
        ImGui::DragFloat("Prev Timer", &comp.prevTimer, 0.01f, 0.0f, 10.0f);
        ImGui::EndDisabled();

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

REGISTER_COMPONENT(RootMotionComponent, "Animation")
REGISTER_COMPONENT_EDITOR_GUI(RootMotionComponent)