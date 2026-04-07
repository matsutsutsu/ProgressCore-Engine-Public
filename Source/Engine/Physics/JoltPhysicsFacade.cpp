#include "Engine/Physics/JoltPhysicsFacade.h"
#include "Engine/Physics/JoltPhysicsManager.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>

void JoltPhysicsFacade::AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& force) {
    auto* handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;

    JPH::BodyInterface& bodyInterface = m_world->GetResource<JoltPhysicsManager>().GetBodyInterface();
    // XMFLOAT3 を JPH::Vec3 に翻訳して渡す
    bodyInterface.AddForce(handle->bodyID, JPH::Vec3(force.x, force.y, force.z));
    // Facadeの中で自動的にActivateするので、外からは呼ばなくてよい
    bodyInterface.ActivateBody(handle->bodyID);
}

void JoltPhysicsFacade::SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& velocity) {
    auto* handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;

    JPH::BodyInterface& bodyInterface = m_world->GetResource<JoltPhysicsManager>().GetBodyInterface();
    // XMFLOAT3 を JPH::Vec3 に翻訳
    bodyInterface.SetLinearVelocity(handle->bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

DirectX::XMFLOAT3 JoltPhysicsFacade::GetLinearVelocity(CCL::ECS::EntityID entity) {
    auto* handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

    JPH::BodyInterface& bodyInterface = m_world->GetResource<JoltPhysicsManager>().GetBodyInterface();

    // JPH::Vec3 を受け取り、XMFLOAT3 に翻訳して返す！
    JPH::Vec3 vel = bodyInterface.GetLinearVelocity(handle->bodyID);
    return DirectX::XMFLOAT3(vel.GetX(), vel.GetY(), vel.GetZ());
}

bool JoltPhysicsFacade::CheckGrounded(CCL::ECS::EntityID entity, float distance) {
    auto* handle = m_world->GetComponent<JoltHandleComponent>(entity);
    auto* trans = m_world->GetComponent<TransformComponent>(entity);
    if (!handle || !trans) return false;

    auto& physicsManager = m_world->GetResource<JoltPhysicsManager>();
    const JPH::NarrowPhaseQuery& narrowPhase = physicsManager.GetPhysicsSystem()->GetNarrowPhaseQuery();

    JPH::RVec3 rayStart(trans->position.x, trans->position.y, trans->position.z);
    JPH::RVec3 rayDir(0.0f, -distance, 0.0f);
    JPH::RayCastResult hit;
    JPH::IgnoreSingleBodyFilter ignoreSelf(handle->bodyID);

    return narrowPhase.CastRay(
        JPH::RRayCast(rayStart, rayDir), hit,
        JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), ignoreSelf
    );
}