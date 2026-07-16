/// @file  JustEvadeEffectComponentMeta.cpp
/// @brief ジャスト回避演出(JustEvadeEffectComponent)のシリアライズ/インスペクタ定義。
///        ランタイム状態(timer)は Fields() に載せない＝保存対象外。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Impact/JustEvadeEffectComponent.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentMetaJson.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

#ifdef ENABLE_EDITOR
#include "Editor/Inspector/ComponentGuiRegistry.h"
#include "Engine/Serialization/Meta/ComponentMetaImGui.h"
#endif

#include <json.hpp>
using json = nlohmann::json;

// ===========================================================================
// JustEvadeEffectComponent（Effect カテゴリ。シーンに singleton で1つ置く）
//   style は3種の色味を切り替えるドロップダウン（見比べ確認用）。
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JustEvadeEffectComponent, "Just Evade Effect (ジャスト回避演出)", CCL::Categories::Effect, 186, false)
    CFIELD_ENUM_U8(style,        "Color Style",      JustEvadeStyleNames, JustEvadeStyleCount, "Style")
    CFIELD_FLOAT_RANGED(duration,     "Duration (s)",       0.005f, 0.0f, 2.0f,  "Envelope")
    CFIELD_FLOAT_RANGED(attackTime,   "Attack (s)",         0.005f, 0.0f, 0.5f,  "Envelope")
    CFIELD_FLOAT_RANGED(releaseTime,  "Release (s)",        0.005f, 0.0f, 1.0f,  "Envelope")
    CFIELD_FLOAT_RANGED(spikeDecay,   "Spike Decay (s)",    0.005f, 0.0f, 1.0f,  "Envelope")
    CFIELD_FLOAT_RANGED(radialPeak,   "Radial Blur Peak",   0.01f,  0.0f, 2.0f,  "Post Process")
    CFIELD_FLOAT_RANGED(chromaPeak,   "Chromatic Peak",     0.001f, 0.0f, 0.2f,  "Post Process")
    CFIELD_FLOAT_RANGED(gradeMixPeak, "Color Grade Amount", 0.01f,  0.0f, 1.0f,  "Post Process")
    CFIELD_FLOAT_RANGED(vignettePeak, "Vignette Peak",      0.01f,  0.0f, 1.0f,  "Post Process")
    CFIELD_FLOAT_RANGED(vignettePulse,"Vignette Pulse",     0.1f,   0.0f, 20.0f, "Post Process")
COMPONENT_META_END(JustEvadeEffectComponent)
