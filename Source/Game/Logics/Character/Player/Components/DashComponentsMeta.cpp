/// @file DashComponentsMeta.cpp
/// @brief Dash / JustEvade のメタ定義。

#include "DashComponent.h"
#include "JustEvadeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

// ===========================================================================
// DashComponent メタデータ定義
// ★修正: Movement カテゴリへ
// ===========================================================================
COMPONENT_META_BEGIN_FULL(DashComponent, "Dash Component", CCL::Categories::Movement, 155, false)
CFIELD_FLOAT_RANGED(dashSpeed, "ダッシュ速度", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(dashDuration, "ダッシュ継続時間", 0.01f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(dashCooldown, "ダッシュ再使用時間", 0.01f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(justEvadeWindow, "ジャスト回避受付時間", 0.01f, 0.0f, 10.0f, "設定")

CFIELD_FLOAT_RANGED(approachSpeed, "接近速度", 1.0f, 0.0f, 300.0f, "設定")
CFIELD_FLOAT_RANGED(approachDuration, "接近継続時間", 0.01f, 0.0f, 3.0f, "設定")
CFIELD_FLOAT_RANGED(approachPlayerScale, "接近時プレイヤー倍率", 0.05f, 0.1f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(approachStopRange, "接近停止距離", 0.1f, 0.0f, 30.0f, "設定")

CFIELD(isDashing, "ダッシュ中", "状態")
CFIELD(isApproaching, "接近中", "状態")
CFIELD(isInvincible, "無敵中", "状態")
CFIELD(currentTimer, "現在タイマー", "状態")
CFIELD_FLOAT3_FAST(dashDirection, "ダッシュ方向", 0.1f, "状態")
COMPONENT_META_END(DashComponent)

// ===========================================================================
// JustEvadeComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JustEvadeComponent, "Just Evade Component", CCL::Categories::Movement, 156, false)
CFIELD_INT_RANGED(maxChain, "チェイン上限", 1, 20, "チェイン設定")
CFIELD_FLOAT_RANGED(chainSpeedPerLevel, "1段階ごとの速度上昇率", 0.01f, 0.0f, 1.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(chainSpeedMax, "チェイン速度上昇の上限", 0.01f, 0.0f, 3.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(windowScalePerLevel, "1段階ごとのジャスト判定受付窓縮小率", 0.01f, 0.0f, 1.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(windowScaleMin, "ジャスト判定受付窓倍率の下限", 0.01f, 0.05f, 1.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(killRadiusBase, "ドローン撃破半径(1段目)", 0.1f, 0.0f, 30.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(killRadiusPerLevel, "1段階ごとのドローン撃破半径拡大", 0.1f, 0.0f, 30.0f, "チェイン設定")
CFIELD_FLOAT_RANGED(idleBreakTime, "静止切断秒数", 0.1f, 0.0f, 20.0f, "チェイン設定")
CFIELD(isJustSuccess, "ジャスト成功", "状態")
CFIELD_INT_RANGED(chainCount, "現在チェイン段数", 0, 20, "状態")
COMPONENT_META_END(JustEvadeComponent)