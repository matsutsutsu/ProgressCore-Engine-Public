/// @file  JoltPhysicsComponentMeta.cpp
/// @brief Jolt Physics 関連コンポーネント (Collider, Rigidbody) のメタ定義。
///        すべて標準的なデータ構造のため、パターンA (全自動マクロ) でシリアライズとGUIを構築する。

#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Physics/Components/RigidBody/JoltRigidbodyComponent.h"

#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // LayerNames / LayerCount

#include "Engine/Serialization/Meta/ComponentDefine.h"
#include "Engine/Serialization/Meta/ComponentCategories.h"

// ============================================================================
// Jolt Box Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltBoxColliderComponent, "Jolt Box Collider", CCL::Categories::Physics, 210, false)
    CFIELD_FLOAT3_FAST(halfExtent, "Half Extent", 0.1f, "Box Collider")
    CFIELD_FLOAT3_FAST(localOffset, "Local Offset (位置ズレ)", 0.1f, "Box Collider")
    CFIELD_FLOAT3_FAST(localRotationEuler, "Local Rotation (角度ズレ)", 1.0f, "Box Collider")
COMPONENT_META_END(JoltBoxColliderComponent)

// ============================================================================
// Jolt Sphere Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltSphereColliderComponent, "Jolt Sphere Collider", CCL::Categories::Physics, 211, false)
    CFIELD_FLOAT_RANGED(radius, "Radius", 0.1f, 0.01f, 100.0f, "Sphere Collider")
    CFIELD_FLOAT3_FAST(localOffset, "Local Offset (位置ズレ)", 0.1f, "Sphere Collider")
COMPONENT_META_END(JoltSphereColliderComponent)

// ============================================================================
// Jolt Capsule Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltCapsuleColliderComponent, "Jolt Capsule Collider", CCL::Categories::Physics, 212, false)
    CFIELD_FLOAT_RANGED(halfHeight, "Half Height", 0.1f, 0.01f, 100.0f, "Capsule Collider")
    CFIELD_FLOAT_RANGED(radius, "Radius", 0.1f, 0.01f, 100.0f, "Capsule Collider")
    CFIELD_FLOAT3_FAST(localOffset, "Local Offset (位置ズレ)", 0.1f, "Capsule Collider")
    CFIELD_FLOAT3_FAST(localRotationEuler, "Local Rotation (角度ズレ)", 1.0f, "Capsule Collider")
COMPONENT_META_END(JoltCapsuleColliderComponent)

// ============================================================================
// Jolt Mesh Collider
// ============================================================================
COMPONENT_META_BEGIN_FULL(JoltMeshColliderComponent, "Jolt Mesh Collider", CCL::Categories::Physics, 213, false)
    CFIELD(isEnabled, "Enable Mesh Collider", "Mesh Collider")
COMPONENT_META_END(JoltMeshColliderComponent)

// ============================================================================
// Jolt Rigidbody
// ============================================================================
namespace {
    const char* const MotionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
    // ★ObjectLayer の名前は PhysicsLayers::LayerNames を使う（手書き配列を廃止）。
    //   レイヤーを増やしても PhysicsLayers.h の X-Macro に1行足すだけで自動反映される。
} // namespace

COMPONENT_META_BEGIN_FULL(JoltRigidbodyComponent, "Jolt Rigidbody", CCL::Categories::Physics, 200, false)
    CFIELD_ENUM_U8(motionType, "Motion Type", MotionTypeNames, 3, "Physics")
    CFIELD_ENUM_U8(objectLayer, "Object Layer", PhysicsLayers::LayerNames, PhysicsLayers::LayerCount, "Physics")

    CFIELD_FLOAT_RANGED(restitution, "Restitution (反発)", 0.05f, 0.0f, 1.0f, "Material")
    CFIELD_FLOAT_RANGED(friction, "Friction (摩擦)", 0.05f, 0.0f, 1.0f, "Material")

    CFIELD_FLOAT3_FAST(initialVelocity, "Initial Velocity", 0.5f, "Physics Extended")
    CFIELD_FLOAT_RANGED(gravityFactor, "Gravity Factor", 0.1f, -10.0f, 10.0f, "Physics Extended")
    CFIELD(isSensor, "Is Sensor (すり抜け)", "Physics Extended")
    CFIELD(useCCD, "Use CCD (壁抜け防止)", "Physics Extended")
COMPONENT_META_END(JoltRigidbodyComponent)