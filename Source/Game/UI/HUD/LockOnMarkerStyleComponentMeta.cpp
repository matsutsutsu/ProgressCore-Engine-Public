// LockOnMarkerStyleComponent のシリアライズ/インスペクタ定義。
#include "Game/UI/HUD/LockOnMarkerStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(LockOnMarkerStyleComponent, "LockOn Marker Style (ロックオンの照準)", CCL::Categories::UI, 63, false)
CFIELD_COLOR(color, "照準の色", "形")
CFIELD_FLOAT_RANGED(radius, "輪の半径(px)", 0.5f, 2.0f, 128.0f, "形")
CFIELD_FLOAT_RANGED(thickness, "線の太さ(px)", 0.1f, 0.5f, 16.0f, "形")
CFIELD_FLOAT_RANGED(coreSize, "中心の◆の大きさ(px)", 0.1f, 0.0f, 32.0f, "形")
CFIELD_FLOAT_RANGED(arcSpan, "弧の広がり(度)", 1.0f, 10.0f, 180.0f, "形")
CFIELD_FLOAT_RANGED(spinSpeed, "回る速さ(ラジアン/秒)", 0.05f, 0.0f, 10.0f, "動き")
CFIELD_FLOAT_RANGED(acquireTime, "縮みきるまで(秒)", 0.01f, 0.01f, 2.0f, "動き")
CFIELD_FLOAT_RANGED(acquireOpen, "縮む前の開き", 0.05f, 0.0f, 5.0f, "動き")
CFIELD_FLOAT_RANGED(fadeIn, "出る速さ", 0.1f, 0.1f, 60.0f, "動き")
CFIELD_FLOAT_RANGED(fadeOut, "消える速さ", 0.1f, 0.1f, 60.0f, "動き")
COMPONENT_META_END(LockOnMarkerStyleComponent)
