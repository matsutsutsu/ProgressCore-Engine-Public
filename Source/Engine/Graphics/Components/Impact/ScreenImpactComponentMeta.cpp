/// @file  ScreenImpactComponentMeta.cpp
/// @brief 命中演出(ScreenImpactComponent)のシリアライズ/インスペクタ定義。
///        ランタイム状態(timer/power/seed)は Fields() に載せない＝保存対象外。

#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Graphics/Components/Impact/ScreenImpactComponent.h"

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
// ScreenImpactComponent（Effect カテゴリ。シーンに singleton で1つ置く）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(ScreenImpactComponent, "Screen Impact", CCL::Categories::Effect, 185, false)
    CFIELD_FLOAT_RANGED(duration,          "Duration (s)",     0.005f, 0.0f, 1.0f,  "Impact")
    CFIELD_FLOAT_RANGED(shakeAmplitude,    "Shake Amplitude",  0.005f, 0.0f, 2.0f,  "Camera Shake")
    CFIELD_FLOAT_RANGED(shakeFrequency,    "Shake Frequency",  0.5f,   0.0f, 120.0f,"Camera Shake")
    CFIELD_FLOAT_RANGED(radialStrength,    "Radial Blur Peak", 0.01f,  0.0f, 2.0f,  "Post Process")
    CFIELD_FLOAT_RANGED(chromaticStrength, "Chromatic Peak",   0.001f, 0.0f, 0.2f,  "Post Process")
COMPONENT_META_END(ScreenImpactComponent)
