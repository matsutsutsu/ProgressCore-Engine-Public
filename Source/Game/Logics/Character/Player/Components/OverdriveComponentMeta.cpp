/// @file OverdriveComponentMeta.cpp
/// @brief OverdriveComponent のメタ定義（インスペクタ調整・シリアライズ）。
///        ランタイム状態(currentGauge/isActive/wasDashing)は保存対象外。

#include "OverdriveComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// OverdriveComponent メタデータ定義（Combat カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(OverdriveComponent, "Overdrive (オーバードライブ)", CCL::Categories::Combat, 160, false)
    CFIELD_FLOAT_RANGED(maxGauge,        "Max Gauge",          1.0f,   1.0f, 1000.0f, "Gauge")
    CFIELD_FLOAT_RANGED(gainOnJustEvade, "Gain: Just Evade",   0.5f,   0.0f, 200.0f,  "Gauge")
    CFIELD_FLOAT_RANGED(gainOnDash,      "Gain: Dash (救済)",  0.1f,   0.0f, 100.0f,  "Gauge")
    CFIELD_FLOAT_RANGED(duration,        "Duration (s)",       0.1f,   0.5f, 30.0f,   "Activation")
    CFIELD_FLOAT_RANGED(enemyTimeScale,  "Enemy Time Scale",   0.01f,  0.05f, 1.0f,   "Activation")
    CFIELD_FLOAT_RANGED(playerTimeScale, "Player Time Scale",  0.01f,  0.5f, 2.0f,    "Activation")
    CFIELD(currentGauge, "Current Gauge (RT)", "State")
    CFIELD(isActive,     "Is Active (RT)",     "State")
COMPONENT_META_END(OverdriveComponent)
