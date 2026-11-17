#include "Game/UI/Input/InputIconComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <iterator>

namespace
{
    // InputIconKind の並びと同じ順にする
    constexpr const char *kInputIconKindNames[] = {"Action", "Move", "Camera", "MenuConfirm", "MenuNavigate", "Pause"};
}

// アクション名のハッシュは System が生成時・ヒントの切り替え時に書くので保存しない
COMPONENT_META_BEGIN_FULL(InputIconComponent, "Input Icon (操作アイコン)", CCL::Categories::UI, 70, false)
CFIELD_ENUM_U8(kind, "アイコンの種類", kInputIconKindNames, std::size(kInputIconKindNames), "アイコン")
CFIELD_RT(actionHash, "アクション名のハッシュ", "アイコン")
COMPONENT_META_END(InputIconComponent)
