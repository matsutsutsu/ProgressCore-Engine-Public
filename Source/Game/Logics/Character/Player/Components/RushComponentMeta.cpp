// Rush（ジャスト回避成立後のラッシュ）のメタ定義。プレハブ/エディタからの読み書きを可能にする。

#include "RushComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(RushComponent, "Rush Component", CCL::Categories::Movement, 157, false)
CFIELD_FLOAT_RANGED(worldScale, "世界スロー倍率", 0.01f, 0.01f, 1.0f, "設定")
CFIELD_FLOAT_RANGED(playerScale, "自分の時間倍率", 0.05f, 0.1f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(slowRecover, "復帰イーズ", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(lockedDuration, "回避離脱の長さ", 0.01f, 0.0f, 2.0f, "設定")
CFIELD_FLOAT_RANGED(readyDuration, "待機の長さ", 0.01f, 0.0f, 3.0f, "設定")
CFIELD_FLOAT_RANGED(attackDuration, "攻撃状態の長さ", 0.01f, 0.0f, 3.0f, "設定")
CFIELD_FLOAT_RANGED(freeDuration, "自由の長さ", 0.01f, 0.0f, 3.0f, "設定")
CFIELD_FLOAT_RANGED(leapSpeed, "回避離脱の初速", 1.0f, 0.0f, 300.0f, "設定")
CFIELD_FLOAT_RANGED(closeInSpeed, "詰めの速さ", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(closeInStopDistance, "詰めて止まる距離", 0.1f, 0.0f, 20.0f, "設定")
CFIELD_FLOAT_RANGED(invincibleRefresh, "無敵の維持量", 0.01f, 0.0f, 1.0f, "設定")
CFIELD(isActive, "ラッシュ中", "状態")
CFIELD(timer, "残り時間", "状態")
CFIELD(attackFired, "斬り発火済み", "状態")
COMPONENT_META_END(RushComponent)
