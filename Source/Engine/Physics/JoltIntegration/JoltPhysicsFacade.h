#pragma once
// ===========================================================================
// 概要: IPhysicsAPI の Jolt 実装。EntityID から剛体（JoltHandleComponent の BodyID）を引いて操作する。
//
// 責務:
//   - IPhysicsAPI の各操作を JPH::BodyInterface / NarrowPhaseQuery の呼び出しへ変換する
//   - レイキャストで自分自身とセンサーを除外する
//
// 備考: JoltPhysicsManager は World のリソースから毎回引く（保持しない）。
// ===========================================================================
#include "Engine/Physics/JoltIntegration/IPhysicsAPI.h"
#include "Engine/ECS/Core/CCL_World.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>

namespace JPH
{
    class BodyInterface;
    class PhysicsSystem;
} // namespace JPH

class JoltPhysicsFacade : public IPhysicsAPI
{
private:
    CCL::ECS::World *m_world; // 所有しない

public:
    JoltPhysicsFacade(CCL::ECS::World *world) : m_world(world) {}
    virtual ~JoltPhysicsFacade() = default;

    // Linear
    void              AddForce(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &force) override;
    void              AddImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &impulse) override;
    void              SetLinearVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &velocity) override;
    DirectX::XMFLOAT3 GetLinearVelocity(CCL::ECS::EntityID entity) override;

    // Angular
    void              AddTorque(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &torque) override;
    void              AddAngularImpulse(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &impulse) override;
    void              SetAngularVelocity(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &velocity) override;
    DirectX::XMFLOAT3 GetAngularVelocity(CCL::ECS::EntityID entity) override;

    // Warp
    void SetPosition(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &position) override;
    void SetRotation(CCL::ECS::EntityID entity, const DirectX::XMFLOAT4 &rotation) override;

    void SetLookDirection(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &direction) override;

    // Queries (剛体)
    bool           CheckGrounded(CCL::ECS::EntityID entity, float distance) override;
    PhysicsHitInfo RayCast(const DirectX::XMFLOAT3 &origin, const DirectX::XMFLOAT3 &direction, float maxDistance, CCL::ECS::EntityID ignoreEntity) override;
    PhysicsHitInfo RayCastStatic(const DirectX::XMFLOAT3 &origin, const DirectX::XMFLOAT3 &direction, float maxDistance) override;

    // Queries (CharacterVirtual)
    bool IsCharacterGrounded(CCL::ECS::EntityID entity) override;

private:
    /**
     * @brief EntityID から「操作対象の BodyID」と「BodyInterface」を解決する。
     *
     * JoltHandleComponent と JoltPhysicsManager リソースの存在も確認する。
     * 操作系の全メソッドはこれを通してから JPH を呼ぶ。
     *
     * @return 解決できたら true。false のとき出力引数は不定なので触らないこと。
     */
    bool ResolveBody(CCL::ECS::EntityID entity,
                     JPH::BodyID &outBodyID, JPH::BodyInterface *&outInterface);

    // 剛体を指定しない問い合わせ用。PhysicsSystem だけ取れればよいとき。無ければ nullptr
    JPH::PhysicsSystem *GetPhysicsSystem() const;
};