// ===========================================================================
// Systems/BulletCollisionSystem.cpp
// ---------------------------------
// @file
// @brief BulletCollisionSystem の実体。線分RayCast→OnHit命令→ダメージ→弾破棄。
// ===========================================================================
#include "Game/Logics/Wand/Systems/BulletCollisionSystem.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Game/Logics/Wand/Data/CompiledWandProgram.h"
#include "Game/Logics/Wand/Registry/MagicBulletPool.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"
#include "Engine/Physics/Components/Jolt/JoltHandleComponent.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Body/Body.h>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    /// @brief 線分RayCast結果。
    struct BulletRayHit
    {
        bool     hit = false;
        EntityID entity = InvalidEntityID;
        Vector3  normal = { 0.0f, 1.0f, 0.0f };
    };

    /**
     * @brief origin から dir 方向へ maxDist だけRayCastする（JoltPhysicsFacade の手法を流用）。
     * @param ignore この剛体は無視する（撃ち主の自己ヒット防止）。
     */
    BulletRayHit CastSegment(JoltPhysicsManager* mgr, Core::World* world,
        const Vector3& origin, const Vector3& dir,
        float maxDist, EntityID ignore)
    {
        BulletRayHit out;
        const JPH::NarrowPhaseQuery& np = mgr->GetPhysicsSystem()->GetNarrowPhaseQuery();

        JPH::RVec3 start(origin.x, origin.y, origin.z);
        JPH::RVec3 rayDir(dir.x * maxDist, dir.y * maxDist, dir.z * maxDist);

        // 撃ち主の剛体を無視する。
        JPH::BodyID ignoreId;
        if (ignore != InvalidEntityID)
            if (auto* h = world->GetComponent<JoltHandleComponent>(ignore))
                ignoreId = h->bodyID;
        JPH::IgnoreSingleBodyFilter ignoreFilter(ignoreId);

        JPH::RayCastResult hit;
        const bool isHit = np.CastRay(
            JPH::RRayCast(start, rayDir), hit,
            JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), ignoreFilter);
        if (!isHit) return out;

        out.hit = true;
        JPH::RVec3 hp = start + rayDir * hit.mFraction;

        const JPH::BodyLockInterface& lockIf = mgr->GetPhysicsSystem()->GetBodyLockInterface();
        JPH::BodyLockRead lock(lockIf, hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& b = lock.GetBody();
            out.entity = static_cast<EntityID>(b.GetUserData());
            JPH::Vec3 n = b.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hp);
            out.normal = { n.GetX(), n.GetY(), n.GetZ() };
        }
        return out;
    }

    /// @brief OnHit命令の解釈結果。
    struct OnHitResult { bool pierce = false; };

    /**
     * @brief OnHitフェーズの命令列を解釈する。
     * @note 現状 Accelerate のみ。Accelerate は「貫通時のみ加速」なので、pierce が立つまで休眠。
     *       pierce を立てる概念(Focus等)は後工程。順序依存を避けるため将来は2パス化する。
     */
    OnHitResult ApplyOnHitOps(const CompiledWandProgram& prog, BulletStateComponent& st)
    {
        OnHitResult r;
        for (uint8_t i = 0; i < prog.onHitCount; ++i)
        {
            const EffectOp& op = prog.onHitOps[i];
            switch (op.type)
            {
            case EffectType::Accelerate:
                // params[0] = 貫通時の加速倍率。pierce が立っている時だけ効く。
                if (r.pierce)
                    for (uint16_t s = 0; s < op.stackCount; ++s) st.speed *= op.params[0];
                break;

                // TODO(後工程): Focus(r.pierce=true) / Explosion(着弾範囲) / Reflection(法線で反射)
                //               / Split(3方向分裂) / Slow(対象にスロー付与) / Homing(マーカー化) ...
            default:
                break;
            }
        }
        return r;
    }

} // anonymous namespace

void BulletCollisionSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>())   return;
    if (!_world->HasResource<JoltPhysicsManager*>())   return;  
    if (!_world->HasResource<MagicBulletPool>())       return;

    MagicBulletPool& pool = _world->GetResource<MagicBulletPool>();
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();
    JoltPhysicsManager* mgr = _world->GetResource<JoltPhysicsManager*>();

    // ダメージ適用・弾破棄を行うため直列実行。
    ForEachWithID([&](EntityID id,
        const TransformComponent& t,
        BulletStateComponent& st,
        const BulletProgramRef& ref,
        const BulletTag&          /*tag*/)
        {
            // このフレームに移動した線分（前位置→現在位置）を求める。
            const Vector3 cur = t.position;
            const Vector3 seg = cur - st.prevPosition;
            const float   len = seg.Length();
            if (len < 1e-4f) return; // 生成直後など、まだ動いていない弾はスキップ。

            Vector3 dir = seg / len;

            constexpr float kRadiusPad = 0.2f; // 弾の見かけ半径ぶん少し先まで判定。
            BulletRayHit hitInfo = CastSegment(mgr, _world, st.prevPosition, dir,
                len + kRadiusPad, st.owner);
            if (!hitInfo.hit) return;

            const EntityID target = hitInfo.entity;
            if (!registry.IsValid(ref.programId)) return;
            const CompiledWandProgram& prog = registry.Get(ref.programId);

            // OnHitフェーズ命令を実行（現状 Accelerate のみ・pierce未実装のため休眠）。
            const OnHitResult r = ApplyOnHitOps(prog, st);

            // ダメージ適用（対象が HealthComponent を持つ場合のみ）。
            if (HealthComponent* hp = _world->GetComponent<HealthComponent>(target))
            {
                hp->currentHealth -= prog.baseDamage;
                //CCL_LOG_INFO(LogCategory::Game, "命中 target=%u dmg=%.1f remainHP=%.1f",                   target, prog.baseDamage, hp->currentHP);
            }

            st.lastHitTarget = target;

            // 貫通でなければ弾を破棄（破棄は遅延適用）。
            if (!r.pierce)
            {
                pool.Unregister(id);  
                _world->Destroy(id);
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletCollisionSystem, Priority::LogicStage::L06_Resolution);