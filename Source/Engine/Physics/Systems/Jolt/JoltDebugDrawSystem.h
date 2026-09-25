#pragma once
// ===========================================================================
// 概要: コライダーの形を ShapeRenderer で描くデバッグ表示 System 群（Box / Sphere / Capsule / Mesh）。
//
// 責務:
//   - 各コライダーを Entity のワールド位置・回転とローカルのずれを反映して描く
//
// 備考: isDebugVisible が true のときだけ描く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

// BoxCollider
class JoltBoxDebugDrawSystem : public CCL::ECS::IfSystem<JoltBoxDebugDrawSystem,
                                                         CCL::ECS::Read<TransformComponent>,
                                                         CCL::ECS::Read<JoltBoxColliderComponent>>
{
public:
    JoltBoxDebugDrawSystem() : IfSystem("JoltBoxDebugDrawSystem") {}
    void Update(float dt) override;
};

// SphereCollider
class JoltSphereDebugDrawSystem : public CCL::ECS::IfSystem<JoltSphereDebugDrawSystem,
                                                            CCL::ECS::Read<TransformComponent>,
                                                            CCL::ECS::Read<JoltSphereColliderComponent>>
{
public:
    JoltSphereDebugDrawSystem() : IfSystem("JoltSphereDebugDrawSystem") {}
    void Update(float dt) override;
};

// CapsuleCollider
class JoltCapsuleDebugDrawSystem : public CCL::ECS::IfSystem<JoltCapsuleDebugDrawSystem,
                                                             CCL::ECS::Read<TransformComponent>,
                                                             CCL::ECS::Read<JoltCapsuleColliderComponent>>
{
public:
    JoltCapsuleDebugDrawSystem() : IfSystem("JoltCapsuleDebugDrawSystem") {}
    void Update(float dt) override;
};

// MeshCollider
class JoltMeshDebugDrawSystem : public CCL::ECS::IfSystem<JoltMeshDebugDrawSystem,
                                                          CCL::ECS::Read<TransformComponent>,
                                                          CCL::ECS::Read<JoltMeshColliderComponent>,
                                                          CCL::ECS::Read<ModelComponent>>
{
public:
    JoltMeshDebugDrawSystem() : IfSystem("JoltMeshDebugDrawSystem") {}
    void Update(float dt) override;
};
