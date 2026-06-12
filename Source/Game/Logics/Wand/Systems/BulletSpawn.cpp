// ===========================================================================
// Systems/BulletSpawn.cpp
// -----------------------
// @file
// @brief BulletSpawn の実体。弾Archetypeをここで一元管理し、生成を共通化する。
// ===========================================================================
#include "Game/Logics/Wand/Systems/BulletSpawn.h"
#include "Game/Logics/Wand/Registry/MagicBulletPool.h"
#include "Game/Logics/Wand/Data/BulletComponents.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Common/CCL_Archetype.h"            // Archetype / ArchetypeHelper ※パス確認
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Engine/ECS/Core/CCL_EntityRef.h"

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    struct SpawnParams { float speed = 0.0f; Vector3 direction = { 0.0f, 0.0f, 1.0f }; };

    /// @brief 与えられた命令列を spawn効果として解釈し、初速・方向を確定する（現状 Accelerate のみ）。
    void ApplySpawnOps(const EffectOp* ops, uint8_t count, SpawnParams& sp)
    {
        for (uint8_t i = 0; i < count; ++i)
        {
            const EffectOp& op = ops[i];
            switch (op.type)
            {
            case EffectType::Accelerate:
                for (uint16_t s = 0; s < op.stackCount; ++s) sp.speed *= op.params[0];
                break;
                // TODO(後工程): Split(弾数+2で複数生成→このSpawnを複数回呼ぶ) / Focus / Slow ...
            default: break;
            }
        }
    }
}

namespace BulletSpawn
{
    EntityID Spawn(Core::World* world, MagicBulletPool& pool, ProgramID programId,
        const CompiledWandProgram& prog, const EffectOp* spawnOps, uint8_t spawnOpCount,
        const Vector3& position, const Vector3& baseDirection,
        EntityID owner, uint8_t inheritanceDepth)
    {
        // 全弾共有の単一Archetype（初回のみ構築してキャッシュ）。
        static const Archetype bulletArch = ArchetypeHelper::Generate<
            TransformComponent, BulletProgramRef, BulletStateComponent, BulletTag, TimeStateComponent > ();

        // 上限ガバナに枠を要求。満杯なら最古弾を退役（破棄要求）。
        EntityID evict = InvalidEntityID;
        pool.TryReserve(evict);
        if (evict != InvalidEntityID) world->Destroy(evict);

        // spawn効果を適用して初速・方向を確定。
        SpawnParams sp;
        sp.speed = prog.baseSpeed;
        sp.direction = baseDirection;
        sp.direction.Normalize();
        ApplySpawnOps(spawnOps, spawnOpCount, sp);

        BulletStateComponent state;
        state.direction = sp.direction;
        state.speed = sp.speed;
        state.age = 0.0f;
        state.lifetime = prog.baseLifetime;
        state.inheritanceDepth = inheritanceDepth;
        state.owner = owner;
        state.prevPosition = position;
        state.lastHitTarget = InvalidEntityID;

        // 生成（遅延適用）。EntityRef からIDを取得する。
        CCL::ECS::Core::EntityRef ref = world->Spawn(bulletArch);
        const EntityID newId = ref.ID(); // ※EntityRefのID取得APIに合わせる(.GetID()/.id/暗黙変換等)
        ref.Set(BulletProgramRef{ programId })
            .Set(state)
            .Patch<TransformComponent>([&](TransformComponent& t)
                {
                    t.position = position;
                    t.isDirty = true;
                });

        pool.Register(newId);
        return newId;
    }
}