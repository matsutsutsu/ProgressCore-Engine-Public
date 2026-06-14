#include "Game/Logics/Wand/Systems/BulletEnemyCollisionSystem.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Game/Logics/Wand/Registry/BulletSpatialHash.h"
#include "Game/Logics/Wand/Data/CompiledWandProgram.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

void BulletEnemyCollisionSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<BulletSpatialHash>())   return;
    if (!_world->HasResource<DungeonGrid>())          return;
    if (!_world->HasResource<WandProgramRegistry>()) return;

    const BulletSpatialHash& hash = _world->GetResource<BulletSpatialHash>();
    const DungeonGrid& grid = _world->GetResource<DungeonGrid>();
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    std::vector<BulletSpatialHash::EnemyEntry> neighbors; // 再利用バッファ

    // HealthComponent 書き込みを含む → 直列
    ForEachWithID([&](EntityID bulletId,
        BulletStateComponent& st,
        const TransformComponent& t,
        const BulletProgramRef& ref,
        const BulletTag&          /*tag*/)
        {
            if (st.hitWall || st.hitEnemy) return; // 既に死亡予約済み

            // 弾の現在セルを求め、近傍エントリを取得
            int ci, cj;
            grid.WorldToCell(t.position.x, t.position.z, ci, cj);
            neighbors.clear();
            hash.QueryNeighbors(ci, cj, neighbors);
            if (neighbors.empty()) return;

            // プログラム取得
            float baseDamage = 10.0f;
            bool  pierce = false;
            if (registry.IsValid(ref.programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref.programId);
                baseDamage = prog.baseDamage;
                // Focus(貫通) は OnHit で pierce を立てる設計
                for (uint8_t i = 0; i < prog.onHitCount; ++i)
                    if (prog.onHitOps[i].type == EffectType::Focus) { pierce = true; break; }
            }

            for (const auto& e : neighbors)
            {
                if (e.id == st.owner)       continue; // 自分（撃ち主）は無視
                if (e.id == st.lastHitTarget) continue; // 直近ヒット対象は多重ヒット防止

                const float dx = t.position.x - e.worldX;
                const float dz = t.position.z - e.worldZ;
                const float distSq = dx * dx + dz * dz;
                const float r = e.radius + 0.15f; // 0.15f = 弾の半径

                if (distSq > r * r) continue; // ヒットなし

                // ダメージ適用（HealthComponent への書き込み）
                if (HealthComponent* hp = _world->GetComponent<HealthComponent>(e.id))
                {
                    hp->currentHealth -= baseDamage;
                    CCL_LOG_INFO(LogCategory::Game, "命中 enemy=%u dmg=%.1f hp=%.1f",
                        e.id, baseDamage, hp->currentHealth);
                }

                st.lastHitTarget = e.id;

                if (!pierce)
                {
                    st.hitEnemy = true; // L07 で BulletInheritanceSystem が破棄/継承する
                    return; // 貫通でなければ最初の1体でループを抜ける
                }
                // 貫通なら続けて次の敵を判定
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletEnemyCollisionSystem, Priority::LogicStage::L06_Resolution);