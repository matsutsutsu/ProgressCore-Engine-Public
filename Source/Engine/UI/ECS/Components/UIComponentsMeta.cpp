/// @file UIComponentsMeta.cpp
/// @brief ECSネイティブUIコンポーネントのメタ定義（インスペクタ編集・シリアライズ）。
///        computed*（レイアウト結果）は保存対象外。

#include "UITransformComponent.h"
#include "UIImageComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// UITransformComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UITransformComponent, "UI Transform (矩形)", CCL::Categories::UI, 10, false)
    CFIELD(anchor, "Anchor (0..1)", "Layout")
    CFIELD(pivot,  "Pivot (0..1)",  "Layout")
    CFIELD(offset, "Offset (px)",   "Layout")
    CFIELD(size,   "Size (px)",     "Layout")
    CFIELD_FLOAT_RANGED(rotation, "Rotation (deg)", 0.5f, -360.0f, 360.0f, "Layout")
    CFIELD_FLOAT_RANGED(scale,    "Scale",          0.01f, 0.01f, 10.0f,   "Layout")
COMPONENT_META_END(UITransformComponent)

// ===========================================================================
// UIImageComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIImageComponent, "UI Image (画像/単色)", CCL::Categories::UI, 20, false)
    CFIELD(texturePath, "Texture Path", "Appearance")
    CFIELD_COLOR(color,  "Color",       "Appearance")
    CFIELD(visible,      "Visible",     "Appearance")
    CFIELD_INT_RANGED(sortOrder, "Sort Order", -1000, 1000, "Appearance")
COMPONENT_META_END(UIImageComponent)
