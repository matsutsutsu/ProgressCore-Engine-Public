// ===========================================================================
// Systems/BulletInheritanceSystem.cpp
// -----------------------------------
// @file @brief 寿命切れ弾の継承（再射出）と回収。
// ===========================================================================
#include "Game/Logics/Wand/Systems/BulletInheritanceSystem.h"
#include "Game/Logics/Wand/Systems/BulletSpawn.h"
#include "Game/Logics/Wand/Registry/WandProgramRegistry.h"
#include "Game/Logics/Wand/Registry/MagicBulletPool.h"
#include "Game/Logics/Wand/Data/CompiledWandProgram.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void BulletInheritanceSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>()) return;
    if (!_world->HasResource<MagicBulletPool>())     return;
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();
    MagicBulletPool& pool = _world->GetResource<MagicBulletPool>();

    ForEachWithID([&](EntityID id,
        const TransformComponent& t,
        const BulletStateComponent& st,
        const BulletProgramRef& ref,
        const BulletTag&        /*tag*/)
        {
            if (st.age < st.lifetime) return; // まだ寿命内

            // 継承：深度0かつ継承命令があれば、死ぬ直前に新弾を再射出。
            // 新弾は同じ programId（travel/onHit はそのまま継承）、spawn効果に inheritanceOps を使い、深度1で生む。
            if (st.inheritanceDepth == 0 && registry.IsValid(ref.programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref.programId);
                if (prog.inheritanceCount > 0)
                {
                    BulletSpawn::Spawn(_world, pool, ref.programId, prog,
                        prog.inheritanceOps, prog.inheritanceCount,
                        t.position, st.direction, st.owner,
                        /*inheritanceDepth=*/1);
                }
            }

            // 寿命切れ弾を回収（プールからも解除）。
            pool.Unregister(id);
            _world->Destroy(id);
        });
}

REGISTER_LOGIC_SYSTEM(BulletInheritanceSystem, Priority::LogicStage::L07_Cleanup);