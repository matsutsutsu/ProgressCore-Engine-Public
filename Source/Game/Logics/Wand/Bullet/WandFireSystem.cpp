// ===========================================================================
// Systems/WandFireSystem.cpp
// --------------------------
// @file @brief 発射意図とクールダウンを見て BulletSpawn 経由で弾を生成するSystem。
// ===========================================================================
#include "Game/Logics/Wand/Bullet/WandFireSystem.h"
#include "Game/Logics/Wand/Bullet/BulletSpawn.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Bullet/MagicBulletPool.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void WandFireSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<WandProgramRegistry>()) return;
    if (!_world->HasResource<MagicBulletPool>())
    {
        CCL_LOG_ERROR(LogCategory::Game, "MagicBulletPool 未登録。発射をスキップします。");
        return;
    }
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();
    MagicBulletPool& pool = _world->GetResource<MagicBulletPool>();

    ForEachWithID([&](EntityID id,
        WandRuntimeComponent& rt,
        const WandBuildComponent& build,
        const WandFireIntentComponent& intent,
        const TimeStateComponent& time)
        {
            if (rt.cooldownTimer > 0.0f) rt.cooldownTimer -= time.localDt;

            if (!intent.wantFire)                          return;
            if (rt.cooldownTimer > 0.0f)                   return;
            if (build.cachedProgramId == InvalidProgramID) return;
            if (!registry.IsValid(build.cachedProgramId))  return;

            const CompiledWandProgram& prog = registry.Get(build.cachedProgramId);

            // 通常発射：spawnOps を適用、継承深度0。Split実装後はここを弾数ぶんループする。
            BulletSpawn::Spawn(_world, pool, build.cachedProgramId, prog,
                prog.spawnOps, prog.spawnCount,
                intent.muzzlePosition, intent.aimDirection,
                id, /*inheritanceDepth=*/0);

            rt.cooldownTimer = prog.cooldown;
        });
}

REGISTER_LOGIC_SYSTEM(WandFireSystem, Priority::LogicStage::L02_Update);