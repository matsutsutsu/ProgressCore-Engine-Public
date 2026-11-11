#include "Game/UI/Input/InputPromptStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(InputPromptStyleComponent, "Input Prompt Style (操作の説明)", CCL::Categories::UI, 65, false)
CFIELD_FLOAT_RANGED(iconSize, "アイコンの大きさ(px)", 0.5f, 8.0f, 128.0f, "大きさ")
CFIELD_FLOAT_RANGED(rowHeight, "行の高さ(px)", 0.5f, 8.0f, 160.0f, "大きさ")
CFIELD_FLOAT_RANGED(labelWidth, "行名の幅(px)", 0.5f, 20.0f, 320.0f, "大きさ")
CFIELD_FLOAT_RANGED(labelGap, "アイコンと行名の間(px)", 0.5f, 0.0f, 80.0f, "大きさ")
CFIELD_FLOAT_RANGED(textSize, "行名の文字の高さ(px)", 0.5f, 6.0f, 64.0f, "大きさ")
CFIELD_FLOAT_RANGED(marginRight, "右端からの余白(px)", 0.5f, 0.0f, 400.0f, "配置")
CFIELD_FLOAT_RANGED(marginTop, "上端からの余白(px)", 0.5f, 0.0f, 600.0f, "配置")
CFIELD_FLOAT_RANGED(alpha, "不透明度", 0.01f, 0.0f, 1.0f, "配置")
COMPONENT_META_END(InputPromptStyleComponent)
