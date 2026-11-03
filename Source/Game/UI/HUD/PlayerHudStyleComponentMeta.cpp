// PlayerHudStyleComponent のシリアライズ/インスペクタ定義。
#include "Game/UI/HUD/PlayerHudStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(PlayerHudStyleComponent, "Player HUD Style (HPゲージの振る舞い)", CCL::Categories::UI, 60, false)
CFIELD_COLOR(fillNormal, "普段の色", "色")
CFIELD_COLOR(fillLow, "瀕死の色", "色")
CFIELD_FLOAT_RANGED(lowHpStart, "赤くなり始める割合", 0.01f, 0.0f, 1.0f, "瀕死")
CFIELD_FLOAT_RANGED(lowHpFull, "真っ赤になる割合", 0.01f, 0.0f, 1.0f, "瀕死")
CFIELD_FLOAT_RANGED(pulseHz, "脈打ちの速さ(回/秒)", 0.05f, 0.0f, 8.0f, "瀕死")
CFIELD_FLOAT_RANGED(pulseDepth, "脈打ちの深さ", 0.01f, 0.0f, 1.0f, "瀕死")
CFIELD_FLOAT_RANGED(trailHold, "減り始めるまで(秒)", 0.01f, 0.0f, 2.0f, "被弾")
CFIELD_FLOAT_RANGED(trailSpeed, "減る速さ", 0.1f, 0.1f, 30.0f, "被弾")
CFIELD_FLOAT_RANGED(shakeDecay, "揺れが収まる速さ", 0.1f, 0.1f, 40.0f, "被弾")
COMPONENT_META_END(PlayerHudStyleComponent)
