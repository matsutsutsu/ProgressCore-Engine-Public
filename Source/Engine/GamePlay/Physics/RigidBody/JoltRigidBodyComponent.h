#pragma once
#include "Engine/Physics/JoltPhysicsManager.h" // PhysicsLayers用
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/MotionType.h>

// ユーザーがエディタやJSONで付与し、物理の性質を設定するためのコンポーネント
struct JoltRigidbodyComponent {
    JPH::EMotionType motionType  = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer objectLayer = PhysicsLayers::MOVING;
    float            restitution = 0.0f; // 反発係数
    float            friction    = 0.2f; // 摩擦係数
};