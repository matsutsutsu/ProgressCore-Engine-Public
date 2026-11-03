// BossHudStyleComponent のシリアライズ/インスペクタ定義。
#include "Game/UI/HUD/BossHudStyleComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

COMPONENT_META_BEGIN_FULL(BossHudStyleComponent, "Boss HUD Style (ボスHPゲージの振る舞い)", CCL::Categories::UI, 61, false)
CFIELD_FLOAT_RANGED(trailHold, "減り始めるまで(秒)", 0.01f, 0.0f, 2.0f, "被弾")
CFIELD_FLOAT_RANGED(trailSpeed, "減る速さ", 0.1f, 0.1f, 30.0f, "被弾")
CFIELD_FLOAT_RANGED(shakeDecay, "揺れが収まる速さ", 0.1f, 0.1f, 40.0f, "被弾")
CFIELD_FLOAT_RANGED(phase2AuraMin, "暗いときの強さ", 0.01f, 0.0f, 1.0f, "第2形態の光")
CFIELD_FLOAT_RANGED(phase2AuraMax, "明るいときの強さ", 0.01f, 0.0f, 1.0f, "第2形態の光")
CFIELD_FLOAT_RANGED(phase2AuraPulse, "明滅の速さ(回/秒)", 0.01f, 0.0f, 4.0f, "第2形態の光")
COMPONENT_META_END(BossHudStyleComponent)
