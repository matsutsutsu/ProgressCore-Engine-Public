/// @file CharacterMovementInputComponent.cpp
/// @brief CharacterMovementInputComponent のシリアライズおよびインスペクタGUI定義

#include "CharacterMovementInputComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// CharacterMovementInputComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(CharacterMovementInputComponent, "Character Movement Input")

// Vector3 には高速描画用の CFIELD_FLOAT3_FAST を使用
CFIELD_FLOAT3_FAST(desiredVelocity, "Desired Velocity", 0.1f, "Input State")
CFIELD_FLOAT3_FAST(lookTargetPos, "Look Target Pos", 0.1f, "Input State")

// bool 型は基本の CFIELD で自動推論させる
CFIELD(isAttackRequested, "Attack Requested", "Input State")
CFIELD(jumpRequested, "Jump Requested", "Input State")

COMPONENT_META_END(CharacterMovementInputComponent)