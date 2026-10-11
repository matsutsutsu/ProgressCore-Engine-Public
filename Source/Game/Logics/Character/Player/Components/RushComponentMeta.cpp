// Rush（ジャスト回避成立後のラッシュ）のメタ定義。

#include "RushComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(RushComponent, "Rush Component", CCL::Categories::Movement, 157, false)
CFIELD_FLOAT_RANGED(lungeSpeed, "踏込速度", 1.0f, 0.0f, 300.0f, "設定")
CFIELD_FLOAT_RANGED(lungeDuration, "踏込時間", 0.01f, 0.0f, 3.0f, "設定")
CFIELD_FLOAT_RANGED(plantDuration, "固定時間", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(plantFreeze, "固定フリーズ倍率", 0.01f, 0.0f, 1.0f, "設定")
CFIELD_FLOAT_RANGED(focusDuration, "集中時間", 0.01f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(worldScale, "世界スロー倍率", 0.01f, 0.01f, 1.0f, "設定")
CFIELD_FLOAT_RANGED(playerScale, "自分倍率", 0.05f, 0.1f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(slowRecover, "復帰イーズ", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(closeInSpeed, "詰め速度", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(closeInMaxRange, "詰め最大距離", 0.1f, 0.0f, 30.0f, "設定")
CFIELD_FLOAT_RANGED(closeInStopRange, "詰め停止間合い", 0.1f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(closeInDuration, "詰め最長時間", 0.01f, 0.0f, 3.0f, "設定")
CFIELD(timer, "フェーズ残り", "状態")
CFIELD(closeInTimer, "寄せ残り", "状態")
COMPONENT_META_END(RushComponent)
