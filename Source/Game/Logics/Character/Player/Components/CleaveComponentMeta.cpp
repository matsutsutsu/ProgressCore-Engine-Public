// CleaveComponent のメタ定義（シリアライズ／インスペクタ）。

#include "CleaveComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(CleaveComponent, "Cleave Component", CCL::Categories::Movement, 157, false)
CFIELD_FLOAT_RANGED(cleaveSpeed, "突進速度", 1.0f, 0.0f, 300.0f, "設定")
CFIELD_FLOAT_RANGED(cleaveDuration, "突進時間", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(cleaveCooldown, "再使用待ち", 0.01f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(fanRange, "扇の半径", 0.1f, 0.0f, 30.0f, "設定")
CFIELD_FLOAT_RANGED(fanHalfAngle, "扇の半開き角(度)", 1.0f, 0.0f, 180.0f, "設定")
CFIELD_FLOAT_RANGED(knockbackSpeed, "吹き飛ばし初速", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(knockbackTime, "吹き飛ばし無効時間", 0.1f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(slowDuration, "スロー時間", 0.01f, 0.0f, 3.0f, "演出")

CFIELD_FLOAT_RANGED(slowScale1, "スロー倍率 段1", 0.01f, 0.0f, 1.0f, "段階")
CFIELD_FLOAT_RANGED(slowScale2, "スロー倍率 段2", 0.01f, 0.0f, 1.0f, "段階")
CFIELD_FLOAT_RANGED(slowScale3, "スロー倍率 段3", 0.01f, 0.0f, 1.0f, "段階")
CFIELD_FLOAT_RANGED(readyDuration1, "準備猶予 段1", 0.01f, 0.0f, 5.0f, "段階")
CFIELD_FLOAT_RANGED(readyDuration2, "準備猶予 段2", 0.01f, 0.0f, 5.0f, "段階")
CFIELD_FLOAT_RANGED(readyDuration3, "準備猶予 段3", 0.01f, 0.0f, 5.0f, "段階")
CFIELD(enableStageDecay, "段階減衰ON", "段階")

CFIELD_FLOAT_RANGED(maxGauge, "ゲージ最大", 1.0f, 1.0f, 1000.0f, "ゲージ")
CFIELD_INT_RANGED(maxStock, "ストック上限", 1, 9, "ゲージ")
CFIELD_FLOAT_RANGED(gaugeOnJustEvade, "ジャスト回避で加算", 1.0f, 0.0f, 1000.0f, "ゲージ")
CFIELD_FLOAT_RANGED(gaugeOnDash, "ダッシュで加算", 1.0f, 0.0f, 1000.0f, "ゲージ")

CFIELD_INT_RANGED(hitCount, "直近ヒット数", 0, 1000, "状態")
CFIELD_INT_RANGED(chainCount, "連鎖数", 0, 1000, "状態")
CFIELD_INT_RANGED(stock, "ストック", 0, 9, "状態")
COMPONENT_META_END(CleaveComponent)
