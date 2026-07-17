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

CFIELD_FLOAT3_FAST(desiredVelocity, "要求速度", 0.1f, "入力状態")
CFIELD_FLOAT3_FAST(lookTargetPos, "注視目標座標", 0.1f, "入力状態")

CFIELD(isAttackRequested, "攻撃要求", "入力状態")
CFIELD(jumpRequested, "ジャンプ要求", "入力状態")

CFIELD(dashRequested, "ダッシュ要求", "入力状態")
CFIELD_FLOAT_RANGED(overrideSpeed, "速度上書き", 0.5f, -1.0f, 200.0f, "入力状態")

CFIELD_FLOAT3_FAST(currentVelocity, "現在速度", 0.1f, "状態")

COMPONENT_META_END(CharacterMovementInputComponent)