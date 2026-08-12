/// @file CleaveComponentMeta.cpp
/// @brief CleaveComponent のメタ定義（シリアライズ／インスペクタ）。

#include "CleaveComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ===========================================================================
// CleaveComponent メタデータ定義（Movement カテゴリ）
// ===========================================================================
COMPONENT_META_BEGIN_FULL(CleaveComponent, "Cleave Component", CCL::Categories::Movement, 157, false)
CFIELD_FLOAT_RANGED(cleaveSpeed, "突進速度", 1.0f, 0.0f, 300.0f, "設定")
CFIELD_FLOAT_RANGED(cleaveDuration, "突進時間", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(cleaveCooldown, "再使用待ち", 0.01f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(readyDuration, "準備の猶予", 0.05f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(fanRange, "扇の半径", 0.1f, 0.0f, 30.0f, "設定")
CFIELD_FLOAT_RANGED(fanHalfAngle, "扇の半開き角(度)", 1.0f, 0.0f, 180.0f, "設定")
CFIELD_FLOAT_RANGED(knockbackSpeed, "吹き飛ばし初速", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(knockbackTime, "吹き飛ばし無効時間", 0.1f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(slowScale, "スロー倍率(敵)", 0.01f, 0.0f, 1.0f, "演出")
CFIELD_FLOAT_RANGED(slowDuration, "スロー時間", 0.01f, 0.0f, 3.0f, "演出")

CFIELD_INT_RANGED(hitCount, "直近ヒット数", 0, 1000, "状態")
CFIELD_INT_RANGED(chainCount, "連鎖数", 0, 1000, "状態")
COMPONENT_META_END(CleaveComponent)
