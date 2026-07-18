/// @file UIComponentsMeta.cpp
/// @brief ECSネイティブUIコンポーネントのメタ定義（インスペクタ編集・シリアライズ）。
///        computed*（レイアウト結果）は保存対象外。

#include "UITransformComponent.h"
#include "UIImageComponent.h"
#include "UITextComponent.h"
#include "UIInteractableComponent.h"
#include "UIButtonComponent.h"
#include "UIProgressBarComponent.h"
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

// ===========================================================================
// UITextComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UITextComponent, "UI Text (文字列)", CCL::Categories::UI, 30, false)
    CFIELD(text, "Text", "Appearance")
    CFIELD_FLOAT_RANGED(pixelSize, "Pixel Size", 0.5f, 4.0f, 256.0f, "Appearance")
    CFIELD_COLOR(color, "Color", "Appearance")
    CFIELD(visible,     "Visible",    "Appearance")
    CFIELD_INT_RANGED(sortOrder, "Sort Order", -1000, 1000, "Appearance")
COMPONENT_META_END(UITextComponent)

// ===========================================================================
// UIInteractableComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIInteractableComponent, "UI Interactable (操作可)", CCL::Categories::UI, 40, false)
    CFIELD(interactable, "Interactable", "Input")
    CFIELD(focusable,    "Focusable",    "Input")
    CFIELD_INT_RANGED(priority, "Hit Priority", -1000, 1000, "Input")
COMPONENT_META_END(UIInteractableComponent)

// ===========================================================================
// UIButtonComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIButtonComponent, "UI Button (ボタン)", CCL::Categories::UI, 50, false)
    CFIELD_COLOR(normalColor,   "Normal Color",   "Button")
    CFIELD_COLOR(hoverColor,    "Hover Color",    "Button")
    CFIELD_COLOR(pressedColor,  "Pressed Color",  "Button")
    CFIELD_COLOR(disabledColor, "Disabled Color", "Button")
    CFIELD(onClickAction, "On Click Action", "Button")
COMPONENT_META_END(UIButtonComponent)

// ===========================================================================
// UIProgressBarComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIProgressBarComponent, "UI Progress Bar (ゲージ)", CCL::Categories::UI, 25, false)
    CFIELD_FLOAT_RANGED(value, "Value (0..1)", 0.01f, 0.0f, 1.0f, "Bar")
    CFIELD_COLOR(bgColor,   "BG Color",   "Bar")
    CFIELD_COLOR(fillColor, "Fill Color", "Bar")
    CFIELD(visible,   "Visible",    "Bar")
    CFIELD_INT_RANGED(sortOrder, "Sort Order", -1000, 1000, "Bar")
COMPONENT_META_END(UIProgressBarComponent)
