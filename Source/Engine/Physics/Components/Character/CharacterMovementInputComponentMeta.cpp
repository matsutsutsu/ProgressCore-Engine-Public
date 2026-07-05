/// @file CharacterMovementInputComponent.cpp
/// @brief CharacterMovementInputComponent のシリアライズおよびインスペクタGUI定義

#include "CharacterMovementInputComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

// ===========================================================================
// CharacterMovementInputComponent メタデータ定義
// ★修正: BEGIN_FULL を使用しカテゴリと並び順を明記
// ===========================================================================
COMPONENT_META_BEGIN_FULL(CharacterMovementInputComponent, "Character Movement Input", CCL::Categories::Movement, 150, false)

CFIELD_FLOAT3_FAST(desiredVelocity, "Desired Velocity", 0.1f, "Input State")
CFIELD_FLOAT3_FAST(lookTargetPos, "Look Target Pos", 0.1f, "Input State")

CFIELD(isAttackRequested, "Attack Requested", "Input State")
CFIELD(jumpRequested, "Jump Requested", "Input State")

CFIELD(dashRequested, "Dash Requested", "Input State")
CFIELD_FLOAT_RANGED(overrideSpeed, "Override Speed", 0.5f, -1.0f, 200.0f, "Input State")

CFIELD_FLOAT3_FAST(currentVelocity, "currentVelocity", 0.1f, "State")

COMPONENT_META_END(CharacterMovementInputComponent)