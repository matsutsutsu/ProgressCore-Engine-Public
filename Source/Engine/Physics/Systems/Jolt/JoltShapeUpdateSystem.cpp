#include "JoltShapeUpdateSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/JoltShapeFactory.h" // 形状生成と Result の検証

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>

#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

// ============================================================
// 3つの System に共通する流れ:
//   isDirty を見る → 基本形状を作る → localOffset/localRotation で包む → SetShape → dirty解除
//
// 「基本形状の作り方」だけが形状ごとに違うので、それ以外は
// JoltShapeFactory::CreateOffsetShape に寄せてある（Result の検証も向こう持ち）。
//
// 形状生成に失敗したときは isDirty を下ろしてから抜ける。
// 下ろさないと毎フレーム同じ失敗ログを出し続けてログが埋まるため。
// ============================================================

// ============================================================
// BoxCollider
// ============================================================
void JoltBoxShapeUpdateSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltBoxColliderComponent &box) {
        if (!box.isDirty) return;
        box.isDirty = false;
        if (handle.bodyID.IsInvalid()) return;

        JPH::BoxShapeSettings baseSettings(JPH::Vec3(box.halfExtent.x, box.halfExtent.y, box.halfExtent.z));
        JPH::ShapeRefC        shape = JoltShapeFactory::CreateOffsetShape(
            baseSettings, box.localOffset, box.localRotationEuler, id, "Box");
        if (shape == nullptr) return;

        bodyInterface.SetShape(handle.bodyID, shape, true, JPH::EActivation::Activate);
    });
}

// ============================================================
// SphereCollider
// ============================================================
void JoltSphereShapeUpdateSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltSphereColliderComponent &sphere) {
        if (!sphere.isDirty) return;
        sphere.isDirty = false;
        if (handle.bodyID.IsInvalid()) return;

        JPH::SphereShapeSettings baseSettings(sphere.radius);
        // 球は回転させても意味がないため、回転は常に無回転を渡す
        JPH::ShapeRefC shape = JoltShapeFactory::CreateOffsetShape(
            baseSettings, sphere.localOffset, {0.0f, 0.0f, 0.0f}, id, "Sphere");
        if (shape == nullptr) return;

        bodyInterface.SetShape(handle.bodyID, shape, true, JPH::EActivation::Activate);
    });
}

// ============================================================
// CapsuleCollider
// ============================================================
void JoltCapsuleShapeUpdateSystem::Update(float dt)
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();

    ForEachWithID([&](CCL::ECS::EntityID id, const JoltHandleComponent &handle, JoltCapsuleColliderComponent &capsule) {
        if (!capsule.isDirty) return;
        capsule.isDirty = false;
        if (handle.bodyID.IsInvalid()) return;

        JPH::CapsuleShapeSettings baseSettings(capsule.halfHeight, capsule.radius);
        JPH::ShapeRefC            shape = JoltShapeFactory::CreateOffsetShape(
            baseSettings, capsule.localOffset, capsule.localRotationEuler, id, "Capsule");
        if (shape == nullptr) return;

        bodyInterface.SetShape(handle.bodyID, shape, true, JPH::EActivation::Activate);
    });
}

REGISTER_LOGIC_SYSTEM(JoltBoxShapeUpdateSystem, Priority::LogicStage::L03_PrePhysics);
REGISTER_LOGIC_SYSTEM(JoltSphereShapeUpdateSystem, Priority::LogicStage::L03_PrePhysics);
REGISTER_LOGIC_SYSTEM(JoltCapsuleShapeUpdateSystem, Priority::LogicStage::L03_PrePhysics);
