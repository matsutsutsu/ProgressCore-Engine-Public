#include "BulletEnemyCollisionSystem.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Collision/BulletSpatialHash.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

void BulletEnemyCollisionSystem::Update(float dt)
{
    if (!_world->HasResource<BulletSpatialHash>())   return;
    if (!_world->HasResource<DungeonGrid>())         return;
    if (!_world->HasResource<WandProgramRegistry>()) return;

    const BulletSpatialHash& hash = _world->GetResource<BulletSpatialHash>();
    const DungeonGrid& grid = _world->GetResource<DungeonGrid>();
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    constexpr float kBulletRadius = 0.15f;
    std::vector<BulletSpatialHash::EnemyEntry> neighbors;

    ForEachWithID([&](EntityID /*bulletId*/,
        BulletStateComponent& st,
        const TransformComponent& t,
        const BulletProgramRef& ref,
        const BulletTag& /*tag*/)
        {
            if (st.hitWall || st.hitEnemy) return;

            int ci, cj;
            grid.WorldToCell(t.position.x, t.position.z, ci, cj);
            neighbors.clear();
            hash.QueryNeighbors(ci, cj, neighbors);
            if (neighbors.empty()) return;

            float baseDamage = 10.0f;
            bool  pierce = false;
            if (registry.IsValid(ref.programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref.programId);
                baseDamage = prog.baseDamage;
                for (uint8_t i = 0; i < prog.onHitCount; ++i)
                    if (prog.onHitOps[i].type == EffectType::Focus) { pierce = true; break; }
            }

            // このフレームの移動線分 A(前)→B(現在)。これに対する掃引で判定する。
            const float ax = st.prevPosition.x, az = st.prevPosition.z;
            const float abx = t.position.x - ax, abz = t.position.z - az;
            const float abLenSq = abx * abx + abz * abz;

            for (const auto& e : neighbors)
            {
                if (e.id == st.owner)         continue;
                if (e.id == st.lastHitTarget) continue;

                // 敵中心と線分ABの最近接点を求め、その距離で円判定（掃引円）
                float tt = 0.0f;
                if (abLenSq > 1e-8f)
                {
                    tt = ((e.worldX - ax) * abx + (e.worldZ - az) * abz) / abLenSq;
                    if (tt < 0.0f) tt = 0.0f; else if (tt > 1.0f) tt = 1.0f;
                }
                const float cx = ax + abx * tt;
                const float cz = az + abz * tt;
                const float dx = e.worldX - cx;
                const float dz = e.worldZ - cz;
                const float distSq = dx * dx + dz * dz;
                const float rr = e.radius + kBulletRadius;
                if (distSq > rr * rr) continue; // 掃引してもかすらない

                if (HealthComponent* hp = _world->GetComponent<HealthComponent>(e.id))
                {
                    hp->currentHealth -= baseDamage;
                    CCL_LOG_INFO(LogCategory::Game, "命中 enemy=%u dmg=%.1f hp=%.1f",
                        e.id, baseDamage, hp->currentHealth);
                }
                st.lastHitTarget = e.id;

                if (!pierce) { st.hitEnemy = true; return; }
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletEnemyCollisionSystem, Priority::LogicStage::L06_Resolution);