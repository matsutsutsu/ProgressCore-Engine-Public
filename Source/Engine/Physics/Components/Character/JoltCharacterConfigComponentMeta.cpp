/// @file JoltCharacterConfigComponent.cpp
/// @brief JoltCharacterConfigComponent のシリアライズおよびインスペクタGUI定義

#include "JoltCharacterConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h" // ★追加

// ===========================================================================
// JoltCharacterConfigComponent メタデータ定義
// ★修正: BEGIN_FULL を使用
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JoltCharacterConfigComponent, "Jolt Character Config", CCL::Categories::Physics, 205, false)

CFIELD_FLOAT_RANGED(maxSlopeAngle, "Max Slope Angle (Deg)", 1.0f, 0.0f, 90.0f, "Movement Limits")
CFIELD_FLOAT_RANGED(maxStepHeight, "Max Step Height", 0.05f, 0.0f, 2.0f, "Movement Limits")

CFIELD_FLOAT_RANGED(walkSpeed, "Walk Speed", 0.1f, 0.0f, 50.0f, "Speed")
CFIELD_FLOAT_RANGED(jumpSpeed, "Jump Speed", 0.1f, 0.0f, 50.0f, "Speed")

CFIELD_FLOAT_RANGED(characterMass, "Character Mass (kg)", 1.0f, 0.1f, 1000.0f, "Physics")

CFIELD_FLOAT_RANGED(gravityScale, "Gravity Scale", 0.1f, 0.0f, 50.0f, "Physics")
CFIELD_FLOAT_RANGED(fallGravityScale, "Fall Gravity Scale", 0.1f, 0.0f, 50.0f, "Physics")

CFIELD_FLOAT_RANGED(deceleration, "減速摩擦力 (deceleration)", 0.1f, 1.0f, 50.0f, "Movement Feel")
CFIELD_FLOAT_RANGED(apexGravityScale, u8"頂点重力倍率 (apexGravityScale)", 0.1f, 0.0f, 10.0f, "Game Feel")
CFIELD_FLOAT_RANGED(apexVelocityThreshold, u8"頂点判定閾値 (apexVelocityThreshold)", 0.1f, 0.0f, 10.0f, "Game Feel")

COMPONENT_META_END(JoltCharacterConfigComponent)