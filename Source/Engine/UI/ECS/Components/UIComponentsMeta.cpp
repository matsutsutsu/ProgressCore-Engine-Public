// ECSネイティブUIコンポーネントのメタ定義（インスペクタ編集・シリアライズ）。
// computed*（レイアウト結果）は保存対象外。

#include "UITransformComponent.h"
#include "UIImageComponent.h"
#include "UITextComponent.h"
#include "UIInteractableComponent.h"
#include "UIButtonComponent.h"
#include "UIProgressBarComponent.h"
#include "UIAnimationComponent.h"
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
    CFIELD(useGradient,  "縦グラデーション", "Appearance")
    CFIELD_COLOR(colorBottom, "下端の色",   "Appearance")
    CFIELD(additive,     "加算合成(発光)",  "Appearance")
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
    CFIELD_FLOAT_RANGED(hoverScale, "Hover Scale", 0.01f, 0.5f, 2.0f, "Button")
    CFIELD(onClickAction, "On Click Action", "Button")
COMPONENT_META_END(UIButtonComponent)

// ===========================================================================
// UIAnimationComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIAnimationComponent, "UI Animation (追従演出)", CCL::Categories::UI, 35, false)
    CFIELD_FLOAT_RANGED(smoothSpeed, "Smooth Speed", 0.1f, 0.1f, 60.0f, "Anim")
    CFIELD_FLOAT_RANGED(targetScale, "Target Scale", 0.01f, 0.1f, 4.0f, "Anim")
COMPONENT_META_END(UIAnimationComponent)

// ===========================================================================
// UIProgressBarComponent（UI カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(UIProgressBarComponent, "UI Progress Bar (ゲージ)", CCL::Categories::UI, 25, false)
    CFIELD_FLOAT_RANGED(value, "Value (0..1)", 0.01f, 0.0f, 1.0f, "Bar")
    CFIELD_COLOR(bgColor,   "BG Color",   "Bar")
    CFIELD_COLOR(fillColor, "Fill Color", "Bar")
    CFIELD(visible,   "Visible",    "Bar")
    CFIELD_INT_RANGED(sortOrder, "Sort Order", -1000, 1000, "Bar")
    CFIELD_FLOAT_RANGED(gradient, "下端の暗さ (0=フラット)", 0.01f, 0.0f, 1.0f, "Look")
    CFIELD_FLOAT_RANGED(shakeAmplitude, "被弾の揺れ幅(px)", 0.1f, 0.0f, 32.0f, "Look")
    CFIELD_FLOAT_RANGED(shakeFrequency, "被弾の揺れ速さ(Hz)", 0.5f, 0.0f, 80.0f, "Look")
COMPONENT_META_END(UIProgressBarComponent)
