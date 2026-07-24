#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Collision/JoltBoxColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltCapsuleColliderComponent.h"
#include "Engine/Physics/Components/Collision/JoltMeshColliderComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

// 1. BoxCollider 可視化システム
class JoltBoxDebugDrawSystem : public CCL::ECS::IfSystem<JoltBoxDebugDrawSystem,
                                     CCL::ECS::Read<TransformComponent>,
                                     CCL::ECS::Read<JoltBoxColliderComponent>> {
public:
    JoltBoxDebugDrawSystem() : IfSystem("JoltBoxDebugDrawSystem") {
        //this->isDebugVisible = true;        
    }
    void Update(float dt) override;
};

// 2. SphereCollider 可視化システム
class JoltSphereDebugDrawSystem : public CCL::ECS::IfSystem<JoltSphereDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<JoltSphereColliderComponent>> {
public:
    JoltSphereDebugDrawSystem() : IfSystem("JoltSphereDebugDrawSystem") {
        //this->isDebugVisible = true;
    }
    void Update(float dt) override;
};

// 3. CapsuleCollider 可視化システム
class JoltCapsuleDebugDrawSystem : public CCL::ECS::IfSystem<JoltCapsuleDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<JoltCapsuleColliderComponent>> {
public:
    JoltCapsuleDebugDrawSystem() : IfSystem("JoltCapsuleDebugDrawSystem") {
        //this->isDebugVisible = true;
    }
    void Update(float dt) override;
};

// 4. MeshCollider 可視化システム
class JoltMeshDebugDrawSystem : public CCL::ECS::IfSystem<JoltMeshDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<JoltMeshColliderComponent>,
                                         CCL::ECS::Read<ModelComponent>> {
public:
    JoltMeshDebugDrawSystem() : IfSystem("JoltMeshDebugDrawSystem") {
        //this->isDebugVisible = true;
    }
    void Update(float dt) override;
};
