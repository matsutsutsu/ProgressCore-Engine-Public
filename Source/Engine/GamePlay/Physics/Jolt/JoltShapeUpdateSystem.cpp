#include "JoltShapeUpdateSystem.h"
#include "ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltPhysicsManager.h"

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

// ===================================================================================
// 1. BoxCollider の更新
// ===================================================================================
void JoltBoxShapeUpdateSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    JPH::BodyInterface &bodyInterface = _world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    // 対象: JoltHandle と JoltBoxCollider の両方を持つエンティティのみ！
    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltBoxColliderComponent &box) {
        
        if (!box.isDirty) return; // ★ 超高速スキップ（ここが最重要）

        JPH::BoxShapeSettings shapeSettings(
            JPH::Vec3(box.halfExtent.x, box.halfExtent.y, box.halfExtent.z));
        JPH::ShapeRefC newShape = shapeSettings.Create().Get();

        // 外科手術（trueを渡して質量も再計算させる）
        bodyInterface.SetShape(handle.bodyID, newShape, true, JPH::EActivation::Activate);
        box.isDirty = false;
    });
}

// ===================================================================================
// 2. SphereCollider の更新
// ===================================================================================
void JoltSphereShapeUpdateSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    JPH::BodyInterface &bodyInterface = _world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltSphereColliderComponent &sphere) {
        
        if (!sphere.isDirty) return; 

        JPH::SphereShapeSettings shapeSettings(sphere.radius);
        JPH::ShapeRefC newShape = shapeSettings.Create().Get();

        bodyInterface.SetShape(handle.bodyID, newShape, true, JPH::EActivation::Activate);
        sphere.isDirty = false;
    });
}

// ===================================================================================
// 3. CapsuleCollider の更新
// ===================================================================================
void JoltCapsuleShapeUpdateSystem::Update(float dt)
{
    if (!_world->HasResource<JoltPhysicsManager>()) return;
    JPH::BodyInterface &bodyInterface = _world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltCapsuleColliderComponent &capsule) {
        
        if (!capsule.isDirty) return; 

        JPH::CapsuleShapeSettings shapeSettings(capsule.halfHeight, capsule.radius);
        JPH::ShapeRefC newShape = shapeSettings.Create().Get();

        bodyInterface.SetShape(handle.bodyID, newShape, true, JPH::EActivation::Activate);
        capsule.isDirty = false;
    });
}

// ===================================================================================
// システムの登録 (PrePhysicsフェーズで実行)
// ===================================================================================
REGISTER_LOGIC_SYSTEM(JoltBoxShapeUpdateSystem,     Priority::LogicStage::PrePhysics);
REGISTER_LOGIC_SYSTEM(JoltSphereShapeUpdateSystem,  Priority::LogicStage::PrePhysics);
REGISTER_LOGIC_SYSTEM(JoltCapsuleShapeUpdateSystem, Priority::LogicStage::PrePhysics);