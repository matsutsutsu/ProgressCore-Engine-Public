#include "Game/Logics/Character/Boss/Components/BossStatusVfxComponent.h"
#include "Engine/Serialization/Registry/ComponentRegistry.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

// BossStatusVfxKind と1対1で並べる
static const char *BossStatusVfxKindNames[] = {
    "Rage",      // 怒り中
    "Exhausted", // 疲れ
};

template <>
struct ComponentMeta<BossStatusVfxComponent>
{
    CCL_META_USING_THIS(BossStatusVfxComponent)

    static constexpr const char *displayName       = "Boss Status VFX";
    static constexpr const char *category          = CCL::Categories::Effect;
    static constexpr int         sortPriority      = 190;
    static constexpr bool        hiddenInInspector = false;
    static constexpr bool        hasCustomGui      = false;
    static constexpr bool        isSerializable    = true;

    static const std::vector<FieldDescriptor> &Fields()
    {
        static const std::vector<FieldDescriptor> fields = {
            CFIELD_ENUM_U8(kind, "出す状態（例：Rage なら怒りの連発の間だけ、このパーティクルを出す）",
                           BossStatusVfxKindNames, std::size(BossStatusVfxKindNames), "Settings")
            // ここから下は実行時の値。保存しない
            CFIELD_RT(wasActive, "出しているか", "State")};
        return fields;
    }
};
REGISTER_COMPONENT(BossStatusVfxComponent, "BossStatusVfxComponent")
REGISTER_COMPONENT_EDITOR_GUI(BossStatusVfxComponent)
