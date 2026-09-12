#pragma once
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace JPH { class BodyInterface; class PhysicsSystem; }

class JoltPhysicsFacade : public IPhysicsAPI {
private:
    CCL::ECS::World* m_world;

public:
    JoltPhysicsFacade(CCL::ECS::World* world) : m_world(world) {}
    virtual ~JoltPhysicsFacade() = default;

    // Linear
    void AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& force) override;
    void AddImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& impulse) override;
    void SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& velocity) override;
    DirectX::XMFLOAT3 GetLinearVelocity(CCL::ECS::EntityID entity) override;

    // Angular
    void AddTorque(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& torque) override;
    void AddAngularImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& impulse) override;
    void SetAngularVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& velocity) override;
    DirectX::XMFLOAT3 GetAngularVelocity(CCL::ECS::EntityID entity) override;

    // Warp
    void SetPosition(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& position) override;
    void SetRotation(CCL::ECS::EntityID entity, const DirectX::XMFLOAT4& rotation) override;

    void SetLookDirection(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3& direction) override;

    // Queries (剛体)
    bool CheckGrounded(CCL::ECS::EntityID entity, float distance) override;
    PhysicsHitInfo RayCast(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float maxDistance, CCL::ECS::EntityID ignoreEntity) override;
    PhysicsHitInfo RayCastStatic(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float maxDistance) override;

    // Queries (CharacterVirtual)
    bool IsCharacterGrounded(CCL::ECS::EntityID entity) override;

private:
    /**
     * @brief EntityID から「操作対象の BodyID」と「BodyInterface」を解決する。
     *
     * @details
     *  全ての操作系メソッドが
     *      handle 取得 → null なら return → Manager 取得 → BodyInterface 取得
     *  という同じ4行を書いていた（11箇所）。ここに集約する。
     *
     *  ついでに JoltPhysicsManager の存在確認も入れる。System 側は例外なく
     *  HasResource で守っているのに、Facade だけ無条件に GetResource していた。
     *
     * @return 解決できたら true。false のとき出力引数は不定なので触らないこと。
     */
    bool ResolveBody(CCL::ECS::EntityID entity,
        JPH::BodyID &outBodyID, JPH::BodyInterface *&outInterface);

    /// 剛体を持たない問い合わせ用。PhysicsSystem だけ取れれば良いとき。
    JPH::PhysicsSystem *GetPhysicsSystem() const;
};