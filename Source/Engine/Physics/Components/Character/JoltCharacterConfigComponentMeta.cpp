/// @file JoltCharacterConfigComponent.cpp
/// @brief JoltCharacterConfigComponent のシリアライズおよびインスペクタGUI定義

#include "JoltCharacterConfigComponent.h"
#include "Engine/Serialization/Meta/ComponentDefine.h"

// ===========================================================================
// JoltCharacterConfigComponent メタデータ定義
// ===========================================================================
COMPONENT_META_BEGIN(JoltCharacterConfigComponent, "Jolt Character Config")

// CFIELD_FLOAT_RANGED(変数名, "表示名", スライダー速度, 最小値, 最大値, "カテゴリ")
CFIELD_FLOAT_RANGED(maxSlopeAngle, "Max Slope Angle (Deg)", 1.0f, 0.0f, 90.0f, "Movement Limits")
CFIELD_FLOAT_RANGED(maxStepHeight, "Max Step Height", 0.05f, 0.0f, 2.0f, "Movement Limits")

CFIELD_FLOAT_RANGED(walkSpeed, "Walk Speed", 0.1f, 0.0f, 50.0f, "Speed")
CFIELD_FLOAT_RANGED(jumpSpeed, "Jump Speed", 0.1f, 0.0f, 50.0f, "Speed")

CFIELD_FLOAT_RANGED(characterMass, "Character Mass (kg)", 1.0f, 0.1f, 1000.0f, "Physics")

COMPONENT_META_END(JoltCharacterConfigComponent)