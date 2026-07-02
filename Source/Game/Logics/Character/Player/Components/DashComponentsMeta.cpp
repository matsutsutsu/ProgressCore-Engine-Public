#include "DashComponent.h"
#include "JustEvadeComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// DashComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(DashComponent, "Dash Component")

// --- Config (プランナー調整用パラメータ) ---
CFIELD_FLOAT_RANGED(dashSpeed, "Dash Speed", 1.0f, 0.0f, 200.0f, "Config")
CFIELD_FLOAT_RANGED(dashDuration, "Dash Duration", 0.01f, 0.0f, 5.0f, "Config")
CFIELD_FLOAT_RANGED(dashCooldown, "Dash Cooldown", 0.01f, 0.0f, 10.0f, "Config")
CFIELD_FLOAT_RANGED(justEvadeWindow, "Just Evade Window", 0.01f, 0.0f, 10.0f, "Config")

// --- State (実行時の状態監視・デバッグ用) ---
CFIELD(isDashing, "Is Dashing", "State")
CFIELD(isInvincible, "Is Invincible", "State")
CFIELD(currentTimer, "Current Timer", "State")
CFIELD_FLOAT3_FAST(dashDirection, "Dash Direction", 0.1f, "State")

COMPONENT_META_END(DashComponent)

// ===========================================================================
// JustEvadeComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(JustEvadeComponent, "Just Evade Component")

CFIELD(isJustSuccess, "Is Just Success", "State")

COMPONENT_META_END(JustEvadeComponent)