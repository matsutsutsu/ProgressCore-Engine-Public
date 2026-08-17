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

// presence プロキシの物理レイヤー（剛体と同じ LayerNames を共有）
CFIELD_ENUM_U8(objectLayer, "オブジェクトレイヤー", PhysicsLayers::LayerNames, PhysicsLayers::LayerCount, "物理")

CFIELD_FLOAT_RANGED(maxSlopeAngle, "最大登坂角度(度)", 1.0f, 0.0f, 90.0f, "移動制限")
CFIELD_FLOAT_RANGED(maxStepHeight, "最大段差", 0.05f, 0.0f, 2.0f, "移動制限")

CFIELD_FLOAT_RANGED(walkSpeed, "最大歩行速度（物理上限）", 0.1f, 0.0f, 50.0f, "速度")
CFIELD_FLOAT_RANGED(jumpSpeed, "ジャンプ速度", 0.1f, 0.0f, 50.0f, "速度")

CFIELD_FLOAT_RANGED(characterMass, "質量(kg)", 1.0f, 0.1f, 1000.0f, "物理")

CFIELD_FLOAT_RANGED(gravityScale, "重力倍率", 0.1f, 0.0f, 50.0f, "物理")
CFIELD_FLOAT_RANGED(fallGravityScale, "落下重力倍率", 0.1f, 0.0f, 50.0f, "物理")

CFIELD_FLOAT_RANGED(acceleration, "加速の速さ", 0.1f, 1.0f, 50.0f, "操作感")
CFIELD_FLOAT_RANGED(deceleration, "減速の速さ", 0.1f, 1.0f, 50.0f, "操作感")
CFIELD_FLOAT_RANGED(apexGravityScale, "頂点重力倍率", 0.1f, 0.0f, 10.0f, "操作感")
CFIELD_FLOAT_RANGED(apexVelocityThreshold, "頂点判定閾値", 0.1f, 0.0f, 10.0f, "操作感")

COMPONENT_META_END(JoltCharacterConfigComponent)