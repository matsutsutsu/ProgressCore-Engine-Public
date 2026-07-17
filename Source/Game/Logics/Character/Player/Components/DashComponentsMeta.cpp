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
CFIELD_FLOAT_RANGED(dashSpeed, "Dash Speed", 1.0f, 0.0f, 200.0f, "Config")
CFIELD_FLOAT_RANGED(dashDuration, "Dash Duration", 0.01f, 0.0f, 5.0f, "Config")
CFIELD_FLOAT_RANGED(dashCooldown, "Dash Cooldown", 0.01f, 0.0f, 10.0f, "Config")
CFIELD_FLOAT_RANGED(justEvadeWindow, "Just Evade Window", 0.01f, 0.0f, 10.0f, "Config")

CFIELD_FLOAT_RANGED(approachSpeed, "Approach Speed", 1.0f, 0.0f, 300.0f, "Config")
CFIELD_FLOAT_RANGED(approachDuration, "Approach Duration", 0.01f, 0.0f, 3.0f, "Config")
CFIELD_FLOAT_RANGED(approachPlayerScale, "Approach Player Scale", 0.05f, 0.1f, 5.0f, "Config")
CFIELD_FLOAT_RANGED(approachStopRange, "Approach Stop Range", 0.1f, 0.0f, 30.0f, "Config")

CFIELD(isDashing, "Is Dashing", "State")
CFIELD(isApproaching, "Is Approaching", "State")
CFIELD(isInvincible, "Is Invincible", "State")
CFIELD(currentTimer, "Current Timer", "State")
CFIELD_FLOAT3_FAST(dashDirection, "Dash Direction", 0.1f, "State")
COMPONENT_META_END(DashComponent)

// ===========================================================================
// JustEvadeComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JustEvadeComponent, "Just Evade Component", CCL::Categories::Movement, 156, false)
CFIELD(isJustSuccess, "Is Just Success", "State")
COMPONENT_META_END(JustEvadeComponent)