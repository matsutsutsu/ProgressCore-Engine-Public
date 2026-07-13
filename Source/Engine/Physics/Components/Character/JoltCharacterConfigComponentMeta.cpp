/// @file JoltCharacterConfigComponent.cpp
/// @brief JoltCharacterConfigComponent のシリアライズおよびインスペクタGUI定義

#include "JoltCharacterConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // LayerNames / LayerCount

// ===========================================================================
// JoltCharacterConfigComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN_FULL(JoltCharacterConfigComponent, "Jolt Character Config", CCL::Categories::Physics, 205, false)

// ★presence プロキシの物理レイヤー（剛体と同じ LayerNames を共有）
CFIELD_ENUM_U8(objectLayer, "Object Layer", PhysicsLayers::LayerNames, PhysicsLayers::LayerCount, "Physics")

CFIELD_FLOAT_RANGED(maxSlopeAngle, "Max Slope Angle (Deg)", 1.0f, 0.0f, 90.0f, "Movement Limits")
CFIELD_FLOAT_RANGED(maxStepHeight, "Max Step Height", 0.05f, 0.0f, 2.0f, "Movement Limits")

CFIELD_FLOAT_RANGED(walkSpeed, "最大歩行速度（物理上限）", 0.1f, 0.0f, 50.0f, "Speed")
CFIELD_FLOAT_RANGED(jumpSpeed, "Jump Speed", 0.1f, 0.0f, 50.0f, "Speed")

CFIELD_FLOAT_RANGED(characterMass, "Character Mass (kg)", 1.0f, 0.1f, 1000.0f, "Physics")

CFIELD_FLOAT_RANGED(gravityScale, "Gravity Scale", 0.1f, 0.0f, 50.0f, "Physics")
CFIELD_FLOAT_RANGED(fallGravityScale, "Fall Gravity Scale", 0.1f, 0.0f, 50.0f, "Physics")

CFIELD_FLOAT_RANGED(deceleration, "減速摩擦力 (deceleration)", 0.1f, 1.0f, 50.0f, "Movement Feel")
CFIELD_FLOAT_RANGED(apexGravityScale, u8"頂点重力倍率 (apexGravityScale)", 0.1f, 0.0f, 10.0f, "Game Feel")
CFIELD_FLOAT_RANGED(apexVelocityThreshold, u8"頂点判定閾値 (apexVelocityThreshold)", 0.1f, 0.0f, 10.0f, "Game Feel")

COMPONENT_META_END(JoltCharacterConfigComponent)