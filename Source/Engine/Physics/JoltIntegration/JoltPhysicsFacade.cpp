#include "Engine/Physics/JoltIntegration/JoltPhysicsFacade.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/JoltIntegration/PhysicsLayers.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/Body.h>

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>

#include <cmath>

using namespace DirectX;

// レイキャスト用のボディフィルタ
//   - 指定ボディ（自分自身）を無視する
//   - センサー（攻撃判定などのすり抜けるボディ）はレイを遮らない
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
            // センサーはレイの障害物にしない
            return !inBody.IsSensor();
        }

    private:
        JPH::BodyID m_ignore;
    };
} // namespace

// ---------------------------------------------------------------------------
// 共通の前処理
// ---------------------------------------------------------------------------
bool JoltPhysicsFacade::ResolveBody(
    CCL::ECS::EntityID entity, JPH::BodyID &outBodyID, JPH::BodyInterface *&outInterface)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    if (!handle || handle->bodyID.IsInvalid()) return false;

    if (!m_world->HasResource<JoltPhysicsManager *>()) return false;
    auto *manager = m_world->GetResource<JoltPhysicsManager *>();
    if (!manager) return false;

    outBodyID    = handle->bodyID;
    outInterface = &manager->GetBodyInterface();
    return true;
}



JPH::PhysicsSystem *JoltPhysicsFacade::GetPhysicsSystem() const
{
    if (!m_world->HasResource<JoltPhysicsManager *>()) return nullptr;
    auto *manager = m_world->GetResource<JoltPhysicsManager *>();
    return manager ? manager->GetPhysicsSystem() : nullptr;
}



// ---------------------------------------------------------------------------
// 並進
// ---------------------------------------------------------------------------
void JoltPhysicsFacade::AddForce(CCL::ECS::EntityID entity, const XMFLOAT3 &force)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->AddForce(bodyID, JPH::Vec3(force.x, force.y, force.z));
    bi->ActivateBody(bodyID);
}



void JoltPhysicsFacade::AddImpulse(CCL::ECS::EntityID entity, const XMFLOAT3 &impulse)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->AddImpulse(bodyID, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    bi->ActivateBody(bodyID);
}



void JoltPhysicsFacade::SetLinearVelocity(CCL::ECS::EntityID entity, const XMFLOAT3 &velocity)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->SetLinearVelocity(bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}



XMFLOAT3 JoltPhysicsFacade::GetLinearVelocity(CCL::ECS::EntityID entity)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return XMFLOAT3(0, 0, 0);
    JPH::Vec3 vel = bi->GetLinearVelocity(bodyID);
    return XMFLOAT3(vel.GetX(), vel.GetY(), vel.GetZ());
}



// ---------------------------------------------------------------------------
// 回転
// ---------------------------------------------------------------------------
void JoltPhysicsFacade::AddTorque(CCL::ECS::EntityID entity, const XMFLOAT3 &torque)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->AddTorque(bodyID, JPH::Vec3(torque.x, torque.y, torque.z));
    bi->ActivateBody(bodyID);
}



void JoltPhysicsFacade::AddAngularImpulse(CCL::ECS::EntityID entity, const XMFLOAT3 &impulse)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->AddAngularImpulse(bodyID, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    bi->ActivateBody(bodyID);
}



void JoltPhysicsFacade::SetAngularVelocity(CCL::ECS::EntityID entity, const XMFLOAT3 &velocity)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->SetAngularVelocity(bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
}



XMFLOAT3 JoltPhysicsFacade::GetAngularVelocity(CCL::ECS::EntityID entity)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return XMFLOAT3(0, 0, 0);
    JPH::Vec3 vel = bi->GetAngularVelocity(bodyID);
    return XMFLOAT3(vel.GetX(), vel.GetY(), vel.GetZ());
}



// ---------------------------------------------------------------------------
// 位置・回転の強制設定
// ---------------------------------------------------------------------------
void JoltPhysicsFacade::SetPosition(CCL::ECS::EntityID entity, const XMFLOAT3 &position)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->SetPosition(bodyID, JPH::RVec3(position.x, position.y, position.z), JPH::EActivation::Activate);
}



void JoltPhysicsFacade::SetRotation(CCL::ECS::EntityID entity, const XMFLOAT4 &rotation)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;
    bi->SetRotation(bodyID,
                    JPH::Quat(rotation.x, rotation.y, rotation.z, rotation.w).Normalized(),
                    JPH::EActivation::Activate);
}



void JoltPhysicsFacade::SetLookDirection(CCL::ECS::EntityID entity, const DirectX::XMFLOAT3 &direction)
{
    JPH::BodyID         bodyID;
    JPH::BodyInterface *bi;
    if (!ResolveBody(entity, bodyID, bi)) return;

    // Y を無視して水平方向だけを見る。水平成分がほぼ 0 なら回さない
    // （目標に到着した瞬間の微小な差で向きが大きく振れるのを防ぐ）
    const float lenSq = direction.x * direction.x + direction.z * direction.z;
    if (lenSq <= 1e-4f) return;

    // atan2 から Y 軸の回転を直接作る
    // Quat::sFromTo(+Z, dir) は dir が真後ろのとき回転軸が決まらず、ボディが横倒しになることがある
    const float yaw            = std::atan2(direction.x, direction.z);
    JPH::Quat   targetRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw);

    // 回転を適用し、スリープ中なら起こす
    bi->SetRotation(bodyID, targetRotation, JPH::EActivation::Activate);
}



// ---------------------------------------------------------------------------
// 空間クエリ
// ---------------------------------------------------------------------------
bool JoltPhysicsFacade::CheckGrounded(CCL::ECS::EntityID entity, float distance)
{
    auto *handle = m_world->GetComponent<JoltHandleComponent>(entity);
    auto *trans  = m_world->GetComponent<TransformComponent>(entity);
    if (!handle || !trans) return false;

    JPH::PhysicsSystem *physicsSystem = GetPhysicsSystem();
    if (!physicsSystem) return false;
    const JPH::NarrowPhaseQuery &narrowPhase = physicsSystem->GetNarrowPhaseQuery();

    // ローカル欄の position ではなくワールド座標から飛ばす（親を持つ剛体でも正しくする）
    const DirectX::XMFLOAT3 worldPos = trans->GetWorldPosition();
    JPH::RVec3              rayStart(worldPos.x, worldPos.y, worldPos.z);
    JPH::RVec3              rayDir(0.0f, -distance, 0.0f);
    JPH::RayCastResult      hit;

    // 地面(NON_MOVING)だけに当てる。センサー・他キャラ・敵には接地判定を反応させない
    JPH::SpecifiedBroadPhaseLayerFilter bpFilter(BroadPhaseLayers::NON_MOVING);
    JPH::SpecifiedObjectLayerFilter     objFilter(PhysicsLayers::NON_MOVING);
    JPH::IgnoreSingleBodyFilter         ignoreSelf(handle->bodyID);

    return narrowPhase.CastRay(
        JPH::RRayCast(rayStart, rayDir), hit,
        bpFilter, objFilter, ignoreSelf);
}



PhysicsHitInfo JoltPhysicsFacade::RayCast(const XMFLOAT3 &origin, const XMFLOAT3 &direction, float maxDistance, CCL::ECS::EntityID ignoreEntity)
{
    PhysicsHitInfo info;

    JPH::PhysicsSystem *physicsSystem = GetPhysicsSystem();
    if (!physicsSystem) return info;

    // ゼロ長／不正な方向を弾く。XMVector3Normalize は長さ0で NaN を返し、
    //   NaN のレイは Jolt のブロードフェーズを壊す（AABB が NaN 化する）
    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (!(dirLenSq > 1e-12f) || !(maxDistance > 0.0f)) return info;

    const JPH::NarrowPhaseQuery &narrowPhase = physicsSystem->GetNarrowPhaseQuery();

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

    // 自分とセンサーを除外するフィルタを使う（攻撃判定などのセンサーがレイを遮らないように）
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

        const JPH::BodyLockInterface &lockInterface = physicsSystem->GetBodyLockInterface();
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
    PhysicsHitInfo info;

    JPH::PhysicsSystem *physicsSystem = GetPhysicsSystem();
    if (!physicsSystem) return info;

    const float dirLenSq = direction.x * direction.x + direction.y * direction.y + direction.z * direction.z;
    if (!(dirLenSq > 1e-12f) || !(maxDistance > 0.0f)) return info;

    const JPH::NarrowPhaseQuery &narrowPhase = physicsSystem->GetNarrowPhaseQuery();

    JPH::RVec3 rayStart(origin.x, origin.y, origin.z);

    XMVECTOR dirVec = XMVector3Normalize(XMLoadFloat3(&direction));
    dirVec          = XMVectorScale(dirVec, maxDistance);
    XMFLOAT3 finalDir;
    XMStoreFloat3(&finalDir, dirVec);
    JPH::RVec3 rayDir(finalDir.x, finalDir.y, finalDir.z);

    // 地形(NON_MOVING)だけにヒットさせる。プレイヤー/ボス/ドローン等の動的ボディは無視される
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



// ---------------------------------------------------------------------------
// キャラクター専用クエリ
// ---------------------------------------------------------------------------
bool JoltPhysicsFacade::IsCharacterGrounded(CCL::ECS::EntityID entity)
{
    // CharacterVirtual の内部状態を直接参照する（段差・坂・床速度を考慮した正確な接地判定）
    auto *handle = m_world->GetComponent<JoltCharacterHandleComponent>(entity);
    if (!handle || !handle->character) return false;

    return handle->character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}