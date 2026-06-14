// ===========================================================================
// Systems/EnemySpatialHashSystem.cpp
// ----------------------------------
#include "EnemySpatialHashSystem.h"
#include "Game/Logics/Wand/Registry/BulletSpatialHash.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

void EnemySpatialHashSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<BulletSpatialHash>()) return;
    if (!_world->HasResource<DungeonGrid>())       return;

    auto& hash = _world->GetResource<BulletSpatialHash>();
    const auto& grid = _world->GetResource<DungeonGrid>();

    hash.Clear(); // ★毎フレーム再構築

    ForEachWithID([&](EntityID id,
        const TransformComponent& t,
        const EnemyTag&               /*tag*/,
        const JoltSphereColliderComponent& sphere)
        {
            int ci, cj;
            grid.WorldToCell(t.position.x, t.position.z, ci, cj);
            hash.Insert(ci, cj, { id, t.position.x, t.position.z, sphere.radius });
        });
}

// 弾判定(L06)より前の L01 で構築しておく
REGISTER_LOGIC_SYSTEM(EnemySpatialHashSystem, Priority::LogicStage::L01_Input);