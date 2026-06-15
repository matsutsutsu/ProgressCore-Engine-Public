// ===========================================================================
// Bullet/BulletSpawn.cpp
// ----------------------
// @file
// @brief BulletSpawn の実体。弾Archetypeを一元管理し、Spawnフェーズを解決して生成する。
//
//   命名規則:
//     <Name>Spawn        … アタッチメント特有処理（Effects/Effect_<Name>.h に集約）
//     Synergy_<A>_<B>    … 複数アタッチメントの組合せ（このファイルの Pass2 に集約）
//     ResolveSpawn       … フェーズ集約器（switchで <Name>Spawn を呼ぶ → Pass2）
//     BuildFan / SpawnOne … 純粋ジオメトリ / 1発生成プリミティブ（内部）
// ===========================================================================
#include "Game/Logics/Wand/Bullet/BulletSpawn.h"
#include "Game/Logics/Wand/Bullet/MagicBulletPool.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"              // Archetype / ArchetypeHelper ※パス確認
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/ECS/Core/CCL_EntityRef.h"
// --- Jolt 連携（弾を物理空間のセンサーとして生成する）---
#include "Engine/Physics/JoltIntegration/JoltPhysicsManager.h"      // PhysicsLayers
#include "Engine/Physics/Components/RigidBody/JoltRigidBodyComponent.h"
#include "Engine/Physics/Components/Collision/JoltSphereColliderComponent.h"


// アタッチメント特有処理（命名規則: <Name>Spawn）
#include "Game/Logics/Wand/Effects/SpawnResolution.h"
#include "Game/Logics/Wand/Effects/Effect_Accelerate.h"
#include "Game/Logics/Wand/Effects/Effect_Split.h"
#include "Game/Logics/Wand/Effects/Effect_Focus.h"

#include <DirectXMath.h>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{

    // -----------------------------------------------------------------------
    // Spawnフェーズ集約器。Pass1=各opを <Name>Spawn で集約 → Pass2=シナジー。
    // -----------------------------------------------------------------------
    SpawnResolution ResolveSpawn(const EffectOp* ops, uint8_t count)
    {
        SpawnResolution res;

        // フラグを使わず、各ハンドラが r.speedMult や r.spreadDeg を直接乗算・加算する
        for (uint8_t i = 0; i < count; ++i)
        {
            switch (ops[i].type)
            {
            case EffectType::Accelerate: AccelerateSpawn(ops[i], res); break;
            case EffectType::Split:      SplitSpawn(ops[i], res); break;
            case EffectType::Focus:      FocusSpawn(ops[i], res); break;
                // 新しいルーンが増えたらここに追加するだけ
            default: break;
            }
        }
        return res;
    }

    /// @brief baseDir 中心に count 本の方向を左右対称な扇状に生成する（純粋ジオメトリ）。
    int BuildFan(const Vector3& baseDir, int count, float perBulletDeg, Vector3* out, int maxOut)
    {
        Vector3 base = baseDir;
        base.y = 0.0f;
        if (base.LengthSquared() < 1e-6f) base = Vector3(0.0f, 0.0f, 1.0f);
        base.Normalize();

        if (maxOut < 1) return 0;
        if (count < 1)      count = 1;
        if (count > maxOut) count = maxOut;

        if (count == 1) { out[0] = base; return 1; }

        const float totalDeg = perBulletDeg * static_cast<float>(count - 1);
        const float startDeg = -totalDeg * 0.5f;
        for (int i = 0; i < count; ++i)
        {
            const float rad = DirectX::XMConvertToRadians(startDeg + perBulletDeg * static_cast<float>(i));
            const DirectX::SimpleMath::Quaternion q =
                DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(Vector3::UnitY, rad);
            out[i] = Vector3::Transform(base, q);
        }
        return count;
    }

    /// @brief 最終 dir/speed で弾を1発だけ生成するプリミティブ（命令解釈はしない）。
    EntityID SpawnOne(Core::World* world, MagicBulletPool& pool, ProgramID programId,
        const CompiledWandProgram& prog,
        const Vector3& dir, float speed,
        const Vector3& position, EntityID owner, uint8_t inheritanceDepth)
    {
        // 全弾共有の単一Archetype（初回のみ構築してキャッシュ）。
        static const Archetype bulletArch = ArchetypeHelper::Generate<
            TransformComponent, JoltSphereColliderComponent, JoltRigidbodyComponent,
            BulletProgramRef, BulletStateComponent, BulletTag, TimeStateComponent>();

        // 上限ガバナに枠を要求。満杯なら最古弾を退役（破棄要求）。
        EntityID evict = InvalidEntityID;
        pool.TryReserve(evict);
        if (evict != InvalidEntityID) world->Destroy(evict);

        BulletStateComponent state;
        state.direction = dir;
        state.direction.Normalize();
        state.speed = speed;
        state.age = 0.0f;
        state.lifetime = prog.baseLifetime;
        state.inheritanceDepth = inheritanceDepth;
        state.owner = owner;
        state.prevPosition = position;
        state.lastHitTarget = InvalidEntityID;

        // 弾の物理プロファイル：Kinematic（位置はECS駆動）＋センサー＋無重力＋CCD。
        JoltRigidbodyComponent rb;
        rb.motionType = JPH::EMotionType::Kinematic;     // 移動はBulletTravelSystem。Joltは判定だけ。
        rb.objectLayer = PhysicsLayers::BULLET;          // 敵とだけ当たる（弾同士・壁・プレイヤーは当たらない）
        rb.isSensor = true;                              // 物理的に押し合わない（ヒット検知のみ）
        rb.gravityFactor = 0.0f;                         // 落下しない
        rb.useCCD = true;                                // 高速弾のすり抜け防止（敵との掃引はJolt CCDに任せる）

        JoltSphereColliderComponent col;
        col.radius = 0.5f;

        Core::EntityRef ref = world->Spawn(bulletArch);
        const EntityID newId = ref.ID(); // ※EntityRefのID取得APIに合わせる
        ref.Set(BulletProgramRef{ programId })
            .Set(state)
            .Set(rb)
            .Set(col)
            .Patch<TransformComponent>([&](TransformComponent& t)
                {
                    t.position = position;
                    t.isDirty = true;
                });

        pool.Register(newId);
        return newId;
    }
}

namespace BulletSpawn
{
    int Spawn(Core::World* world, MagicBulletPool& pool, ProgramID programId,
        const CompiledWandProgram& prog, const EffectOp* spawnOps, uint8_t spawnOpCount,
        const Vector3& position, const Vector3& baseDirection,
        EntityID owner, uint8_t inheritanceDepth)
    {
        // 1) Spawn命令を解決（集約＋シナジー）
        const SpawnResolution res = ResolveSpawn(spawnOps, spawnOpCount);
        const float finalSpeed  = res.FinalSpeed(prog.baseSpeed);
        const int   volleyCount = res.FinalVolleyCount();
        const float spreadDeg   = res.FinalSpreadDeg();

        // 2) 扇方向を生成（spreadDeg は Synergy_Focus_Split 補正済み）
        Vector3 dirs[32];
        const int n = BuildFan(baseDirection, volleyCount, spreadDeg, dirs, 32);

        // 3) 1発ずつ生成
        for (int i = 0; i < n; ++i)
        {
            SpawnOne(world, pool, programId, prog,
                dirs[i], finalSpeed, position, owner, inheritanceDepth);
        }
        return n;
    }
}