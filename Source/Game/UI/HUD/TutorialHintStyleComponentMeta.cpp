#include "Game/UI/HUD/TutorialHintStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(TutorialHintStyleComponent, "Tutorial Hint Style (ヒントの帯)", CCL::Categories::UI, 62, false)
CFIELD_FLOAT_RANGED(bandHeight, "帯の高さ(px)", 0.5f, 0.0f, 300.0f, "寸法")
CFIELD_FLOAT_RANGED(padX, "帯の左右の余白(px)", 0.5f, 0.0f, 200.0f, "寸法")
CFIELD_FLOAT_RANGED(iconSize, "アイコンの大きさ(px)", 0.5f, 8.0f, 256.0f, "寸法")
CFIELD_FLOAT_RANGED(gap, "部品の間隔(px)", 0.5f, 0.0f, 100.0f, "寸法")
CFIELD_FLOAT_RANGED(textSize, "文字の大きさ(px)", 0.5f, 8.0f, 96.0f, "寸法")
CFIELD_FLOAT_RANGED(emphSize, "「今だ」の文字の大きさ(px)", 0.5f, 8.0f, 96.0f, "寸法")
CFIELD_FLOAT_RANGED(barHeight, "ゲージの太さ(px)", 0.5f, 0.0f, 40.0f, "寸法")
CFIELD_COLOR(fillColor, "ゲージ（進行中）", "色")
CFIELD_COLOR(clearColor, "ゲージ（達成）", "色")
CFIELD_COLOR(textColor, "説明の文字", "色")
CFIELD_COLOR(nowColor, "「今だ」の文字", "色")
CFIELD_FLOAT_RANGED(fadeSpeed, "帯のフェードの速さ", 0.1f, 0.1f, 60.0f, "速さ")
CFIELD_FLOAT_RANGED(fillSpeed, "ゲージの伸びる速さ", 0.1f, 0.1f, 60.0f, "速さ")
CFIELD_FLOAT_RANGED(nowFadeSpeed, "中央のボタンのフェードの速さ", 0.1f, 0.1f, 60.0f, "速さ")
COMPONENT_META_END(TutorialHintStyleComponent)
