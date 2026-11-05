#include "Game/UI/HUD/RushPromptStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(RushPromptStyleComponent, "Rush Prompt Style (ラッシュの合図)", CCL::Categories::UI, 66, false)
CFIELD_FLOAT_RANGED(iconSize, "アイコンの大きさ(px)", 0.5f, 8.0f, 128.0f, "形")
CFIELD_FLOAT_RANGED(ringRadius, "リングの半径(px)", 0.5f, 4.0f, 256.0f, "形")
CFIELD_FLOAT_RANGED(ringThickness, "リングの太さ(px)", 0.1f, 0.5f, 16.0f, "形")
CFIELD_COLOR(ringColor, "残り時間の色", "形")
CFIELD_COLOR(trackColor, "溝の色", "形")
CFIELD_COLOR(warnColor, "残りわずかの色", "形")
CFIELD(targetOffset, "敵からのずれ(px)", "位置")
CFIELD(fallbackAnchor, "ロックオン無しの位置(画面比)", "位置")
CFIELD_FLOAT_RANGED(appearTime, "出て縮みきるまで(秒)", 0.01f, 0.01f, 1.0f, "動き")
CFIELD_FLOAT_RANGED(appearOpen, "出た瞬間の大きさ", 0.05f, 0.0f, 3.0f, "動き")
CFIELD_FLOAT_RANGED(warnRatio, "警告に変わる残り(割合)", 0.01f, 0.0f, 1.0f, "動き")
CFIELD_FLOAT_RANGED(burstTime, "押せたとき消えるまで(秒)", 0.01f, 0.01f, 1.0f, "動き")
CFIELD_FLOAT_RANGED(burstScale, "押せたとき広がる量", 0.05f, 0.0f, 3.0f, "動き")
CFIELD_FLOAT_RANGED(fadeIn, "出る速さ", 0.1f, 0.1f, 60.0f, "動き")
CFIELD_FLOAT_RANGED(fadeOut, "消える速さ", 0.1f, 0.1f, 60.0f, "動き")
COMPONENT_META_END(RushPromptStyleComponent)
