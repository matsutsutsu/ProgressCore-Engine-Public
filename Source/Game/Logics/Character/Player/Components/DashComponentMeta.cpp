// Dash / JustEvade のメタ定義。

#include "DashComponent.h"
#include "JustEvadeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

COMPONENT_META_BEGIN_FULL(DashComponent, "Dash Component", CCL::Categories::Movement, 155, false)
CFIELD_FLOAT_RANGED(dashSpeed, "ダッシュ速度", 1.0f, 0.0f, 200.0f, "設定")
CFIELD_FLOAT_RANGED(dashDuration, "ダッシュ継続時間", 0.01f, 0.0f, 5.0f, "設定")
CFIELD_FLOAT_RANGED(dashCooldown, "ダッシュ再使用時間", 0.01f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(justEvadeWindow, "ジャスト回避受付時間", 0.01f, 0.0f, 10.0f, "設定")
CFIELD_FLOAT_RANGED(invincibleWindow, "回避の無敵時間", 0.01f, 0.0f, 2.0f, "設定")

CFIELD(isDashing, "ダッシュ中", "状態")
CFIELD(isInvincible, "無敵中", "状態")
CFIELD(currentTimer, "現在タイマー", "状態")
CFIELD_FLOAT3_FAST(dashDirection, "ダッシュ方向", 0.1f, "状態")
COMPONENT_META_END(DashComponent)

COMPONENT_META_BEGIN_FULL(JustEvadeComponent, "Just Evade Component", CCL::Categories::Movement, 156, false)
CFIELD(isJustSuccess, "ジャスト成功", "状態")
COMPONENT_META_END(JustEvadeComponent)
