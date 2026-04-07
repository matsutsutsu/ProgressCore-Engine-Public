#pragma once
#include "Engine/Physics/IPhysicsAPI.h"
#include "ECS/Core/CCL_World.h"

class JoltPhysicsFacade : public IPhysicsAPI {
private:
    CCL::ECS::Core::World* m_world;

public:
    JoltPhysicsFacade(CCL::ECS::Core::World* world) : m_world(world) {}
    virtual ~JoltPhysicsFacade() = default;

    void AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& force) override;
    void SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& velocity) override;
    DirectX::XMFLOAT3 GetLinearVelocity(CCL::ECS::EntityID entity) override;
    bool CheckGrounded(CCL::ECS::EntityID entity, float distance) override;
};