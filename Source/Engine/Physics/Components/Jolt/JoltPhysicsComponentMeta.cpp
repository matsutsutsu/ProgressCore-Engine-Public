#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"

#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ============================================================================
// Jolt Box Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltBoxColliderComponent, "Jolt Box Collider", CCL::Categories::Physics, 210, false)
CFIELD_FLOAT3_FAST(halfExtent, "半分の大きさ", 0.1f, "ボックス判定")
CFIELD_FLOAT3_FAST(localOffset, "位置オフセット", 0.1f, "ボックス判定")
CFIELD_FLOAT3_FAST(localRotationEuler, "回転オフセット", 1.0f, "ボックス判定")
COMPONENT_META_END(JoltBoxColliderComponent)

// ============================================================================
// Jolt Sphere Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltSphereColliderComponent, "Jolt Sphere Collider", CCL::Categories::Physics, 211, false)
CFIELD_FLOAT_RANGED(radius, "半径", 0.1f, 0.01f, 100.0f, "球判定")
CFIELD_FLOAT3_FAST(localOffset, "位置オフセット", 0.1f, "球判定")
COMPONENT_META_END(JoltSphereColliderComponent)

// ============================================================================
// Jolt Capsule Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltCapsuleColliderComponent, "Jolt Capsule Collider", CCL::Categories::Physics, 212, false)
CFIELD_FLOAT_RANGED(halfHeight, "半分の高さ", 0.1f, 0.01f, 100.0f, "カプセル判定")
CFIELD_FLOAT_RANGED(radius, "半径", 0.1f, 0.01f, 100.0f, "カプセル判定")
CFIELD_FLOAT3_FAST(localOffset, "位置オフセット", 0.1f, "カプセル判定")
CFIELD_FLOAT3_FAST(localRotationEuler, "回転オフセット", 1.0f, "カプセル判定")
COMPONENT_META_END(JoltCapsuleColliderComponent)

// ============================================================================
// Jolt Mesh Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltMeshColliderComponent, "Jolt Mesh Collider", CCL::Categories::Physics, 213, false)
CFIELD(isEnabled, "メッシュ判定を有効化", "メッシュ判定")
COMPONENT_META_END(JoltMeshColliderComponent)

// ============================================================================
// Jolt Rigidbody
// ============================================================================
namespace
{
    const char *const MotionTypeNames[] = {"Static", "Kinematic", "Dynamic"};
    // ObjectLayer の名前は PhysicsLayers::LayerNames を使う（ここに手書き配列を置かない）
    //   レイヤーを増やしても PhysicsLayers.h の X-Macro に1行足すだけで反映される
} // namespace

COMPONENT_META_BEGIN_FULL(JoltRigidbodyComponent, "Jolt Rigidbody", CCL::Categories::Physics, 200, false)
CFIELD_ENUM_U8(motionType, "モーションタイプ", MotionTypeNames, 3, "物理")
CFIELD_ENUM_U8(objectLayer, "オブジェクトレイヤー", PhysicsLayers::LayerNames, PhysicsLayers::LayerCount, "物理")

CFIELD_FLOAT_RANGED(restitution, "反発係数", 0.05f, 0.0f, 1.0f, "マテリアル")
CFIELD_FLOAT_RANGED(friction, "摩擦係数", 0.05f, 0.0f, 1.0f, "マテリアル")

CFIELD_FLOAT3_FAST(initialVelocity, "初速度", 0.5f, "物理(拡張)")
CFIELD_FLOAT_RANGED(gravityFactor, "重力係数", 0.1f, -10.0f, 10.0f, "物理(拡張)")
CFIELD(isSensor, "センサー(すり抜け)", "物理(拡張)")
CFIELD(useCCD, "CCD(壁抜け防止)", "物理(拡張)")
COMPONENT_META_END(JoltRigidbodyComponent)