#include "Engine/Physics/JoltIntegration/JoltPhysicsFacade.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h" // レイヤー定数（接地判定のフィルタに使う）
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/Body.h> // IsSensor() を使うため

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>

using namespace DirectX;

// ===================================================================================
// レイキャスト用のボディフィルター
//   - 指定ボディ（自分自身）を無視
//   - センサー（弾など「すり抜ける」判定用ボディ）はレイを遮らない
// ===================================================================================
namespace
{
class RaycastBodyFilter final : public JPH::BodyFilter
{
  public:
    explicit RaycastBodyFilter(JPH::BodyID ignore)
        : m_ignore(ignore)
    {
    }

    bool ShouldCollide(const JPH::BodyID &inBodyID) const override
    {
        return inBodyID != m_ignore;
    }

    bool ShouldCollideLocked(const JPH::Body &inBody) const override
    {
        // センサー（弾・トリガー等）はレイの障害物にしない
        return !inBody.IsSensor();
    }

  private:
    JPH::BodyID m_ignore;
};
} // namespace

// =======================================================
// 🔵 Linear (直進)
// =======================================================
void JoltPhysicsFacade::AddForce(CCL::ECS::EntityID entity, const XMFLOAT3 &force)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.AddForce(handle->bodyID, JPH::Vec3(force.x, force.y, force.z));
    bodyInterface.ActivateBody(handle->bodyID);
}

void JoltPhysicsFacade::AddImpulse(CCL::ECS::EntityID entity, const XMFLOAT3 &impulse)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.AddImpulse(handle->bodyID, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    bodyInterface.ActivateBody(handle->bodyID);
}

void JoltPhysicsFacade::SetLinearVelocity(CCL::ECS::EntityID entity, const XMFLOAT3 &velocity)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.SetLinearVelocity(handle->bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

XMFLOAT3 JoltPhysicsFacade::GetLinearVelocity(CCL::ECS::EntityID entity)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return XMFLOAT3(0, 0, 0);
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    JPH::Vec3           vel           = bodyInterface.GetLinearVelocity(handle->bodyID);
    return XMFLOAT3(vel.GetX(), vel.GetY(), vel.GetZ());
}

// =======================================================
// 🟢 Angular (回転)
// =======================================================
void JoltPhysicsFacade::AddTorque(CCL::ECS::EntityID entity, const XMFLOAT3 &torque)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.AddTorque(handle->bodyID, JPH::Vec3(torque.x, torque.y, torque.z));
    bodyInterface.ActivateBody(handle->bodyID);
}

void JoltPhysicsFacade::AddAngularImpulse(CCL::ECS::EntityID entity, const XMFLOAT3 &impulse)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.AddAngularImpulse(handle->bodyID, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    bodyInterface.ActivateBody(handle->bodyID);
}

void JoltPhysicsFacade::SetAngularVelocity(CCL::ECS::EntityID entity, const XMFLOAT3 &velocity)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.SetAngularVelocity(handle->bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}

XMFLOAT3 JoltPhysicsFacade::GetAngularVelocity(CCL::ECS::EntityID entity)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return XMFLOAT3(0, 0, 0);
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    JPH::Vec3           vel           = bodyInterface.GetAngularVelocity(handle->bodyID);
    return XMFLOAT3(vel.GetX(), vel.GetY(), vel.GetZ());
}

// =======================================================
// 🟡 Kinematic Warp (強制制御)
// =======================================================
void JoltPhysicsFacade::SetPosition(CCL::ECS::EntityID entity, const XMFLOAT3 &position)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.SetPosition(handle->bodyID, JPH::RVec3(position.x, position.y, position.z), JPH::EActivation::Activate);
}

void JoltPhysicsFacade::SetRotation(CCL::ECS::EntityID entity, const XMFLOAT4 &rotation)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;
    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();
    bodyInterface.SetRotation(handle->bodyID, JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w), JPH::EActivation::Activate);
}

void JoltPhysicsFacade::SetLookDirection(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &direction)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle) return;

    JPH::BodyInterface &bodyInterface = m_world->GetResource<JoltPhysicsManager *>()->GetBodyInterface();

    // 1. 水平化 (Y軸の無効化): Yaw(左右)だけの回転にする
    JPH::Vec3 flatDirection(direction.x, 0.0f, direction.z);

    // 2. デッドゾーンのチェック（到着時の荒ぶる回転を防止）
    if (flatDirection.LengthSq() > 1e-4f)
    {
        flatDirection = flatDirection.Normalized();

        // 3. 基準の前方ベクトル（+Z）から目標方向へのクォータニオンを生成
        JPH::Quat targetRotation = JPH::Quat::sFromTo(JPH::Vec3(0.0f, 0.0f, 1.0f), flatDirection);

        // 4. 回転を適用し、スリープ状態なら叩き起こす(Activate)
        bodyInterface.SetRotation(handle->bodyID, targetRotation, JPH::EActivation::Activate);
    }
}

// =======================================================
// 🔴 Queries (レイキャスト・接地)
// =======================================================
bool JoltPhysicsFacade::CheckGrounded(CCL::ECS::EntityID entity, float distance)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    auto *trans  = m_world->GetComponent<TransformComponent>(entity);
    if (!handle || !trans) return false;

    auto                        &physicsManager = m_world->GetResource<JoltPhysicsManager *>();
    const JPH::NarrowPhaseQuery &narrowPhase    = physicsManager->GetPhysicsSystem()->GetNarrowPhaseQuery();

    JPH::RVec3         rayStart(trans->position.x, trans->position.y, trans->position.z);
    JPH::RVec3         rayDir(0.0f, -distance, 0.0f);
    JPH::RayCastResult hit;

    // ★fix: 素通しフィルタをやめ、地面(NON_MOVING)だけにヒットさせる。
    //   これで弾(センサー)・他キャラ・敵に接地判定が誤反応しなくなる。
    JPH::SpecifiedBroadPhaseLayerFilter bpFilter(BroadPhaseLayers::NON_MOVING);
    JPH::SpecifiedObjectLayerFilter     objFilter(PhysicsLayers::NON_MOVING);
    JPH::IgnoreSingleBodyFilter         ignoreSelf(handle->bodyID);

    return narrowPhase.CastRay(
        JPH::RRayCast(rayStart, rayDir), hit,
        bpFilter, objFilter, ignoreSelf);
}

PhysicsHitInfo JoltPhysicsFacade::RayCast(const XMFLOAT3 &origin, const XMFLOAT3 &direction, float maxDistance, CCL::ECS::EntityID ignoreEntity)
{
    PhysicsHitInfo               info;
    auto                        &physicsManager = m_world->GetResource<JoltPhysicsManager *>();
    const JPH::NarrowPhaseQuery &narrowPhase    = physicsManager->GetPhysicsSystem()->GetNarrowPhaseQuery();

    JPH::RVec3 rayStart(origin.x, origin.y, origin.z);

    XMVECTOR dirVec = XMVector3Normalize(XMLoadFloat3(&direction));
    dirVec          = XMVectorScale(dirVec, maxDistance);
    XMFLOAT3 finalDir;
    XMStoreFloat3(&finalDir, dirVec);
    JPH::RVec3 rayDir(finalDir.x, finalDir.y, finalDir.z);

    JPH::BodyID ignoreBodyID; // 無効ID で初期化（何も無視しない）
    if (ignoreEntity != CCL::ECS::InvalidEntityID)
    {
        if (auto *handle = m_world->GetComponent<JoltHandleComponent>(ignoreEntity))
        {
            ignoreBodyID = handle->bodyID;
        }
    }

    // ★fix: IgnoreSingleBodyFilter の代わりに、自分＋センサーを除外するフィルタを使う。
    //   （弾同士や自弾のセンサーがレイを遮ってしまう誤爆を防ぐ）
    RaycastBodyFilter bodyFilter(ignoreBodyID);

    JPH::RayCastResult hit;
    bool               isHit = narrowPhase.CastRay(
        JPH::RRayCast(rayStart, rayDir), hit,
        JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), bodyFilter);

    if (isHit)
    {
        info.hit      = true;
        info.distance = maxDistance * hit.mFraction;

        JPH::RVec3 hitPos = rayStart + rayDir * hit.mFraction;
        info.position     = {hitPos.GetX(), hitPos.GetY(), hitPos.GetZ()};

        const JPH::BodyLockInterface &lockInterface = physicsManager->GetPhysicsSystem()->GetBodyLockInterface();
        JPH::BodyLockRead             lock(lockInterface, hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body &body = lock.GetBody();
            info.entity           = static_cast<CCL::ECS::EntityID>(body.GetUserData());

            JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPos);
            info.normal      = {normal.GetX(), normal.GetY(), normal.GetZ()};
        }
    }
    return info;
}

PhysicsHitInfo JoltPhysicsFacade::RayCastStatic(const XMFLOAT3 &origin, const XMFLOAT3 &direction, float maxDistance)
{
    PhysicsHitInfo               info;
    auto                        &physicsManager = m_world->GetResource<JoltPhysicsManager *>();
    const JPH::NarrowPhaseQuery &narrowPhase    = physicsManager->GetPhysicsSystem()->GetNarrowPhaseQuery();

    JPH::RVec3 rayStart(origin.x, origin.y, origin.z);

    XMVECTOR dirVec = XMVector3Normalize(XMLoadFloat3(&direction));
    dirVec          = XMVectorScale(dirVec, maxDistance);
    XMFLOAT3 finalDir;
    XMStoreFloat3(&finalDir, dirVec);
    JPH::RVec3 rayDir(finalDir.x, finalDir.y, finalDir.z);

    // 地形(NON_MOVING)だけにヒットさせる。プレイヤー/ボス/ドローン等の動的ボディは無視される。
    JPH::SpecifiedBroadPhaseLayerFilter bpFilter(BroadPhaseLayers::NON_MOVING);
    JPH::SpecifiedObjectLayerFilter     objFilter(PhysicsLayers::NON_MOVING);

    JPH::RayCastResult hit;
    bool               isHit = narrowPhase.CastRay(
        JPH::RRayCast(rayStart, rayDir), hit, bpFilter, objFilter);

    if (isHit)
    {
        info.hit      = true;
        info.distance = maxDistance * hit.mFraction;

        JPH::RVec3 hitPos = rayStart + rayDir * hit.mFraction;
        info.position     = {hitPos.GetX(), hitPos.GetY(), hitPos.GetZ()};
    }
    return info;
}

// =======================================================
// 🟣 Character Queries (CharacterVirtual)
// =======================================================
bool JoltPhysicsFacade::IsCharacterGrounded(CCL::ECS::EntityID entity)
{
    // CharacterVirtual の内部状態を直接参照する（段差・坂・床速度を考慮した正確な接地判定）。
    auto *handle = m_world->GetComponent<JoltCharacterHandleComponent>(entity);
    if (!handle || !handle->character) return false;

    return handle->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}