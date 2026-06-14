// ===========================================================================
// Systems/CharacterWallSlideSystem.cpp
// ---------------------------------------
#include "Game/Logics/Wand/Systems/CharacterWallSlideSystem.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

void CharacterWallSlideSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<DungeonGrid>()) return;
    const DungeonGrid& grid = _world->GetResource<DungeonGrid>();
    const float cs = grid.CellSize();

    ForEach([&grid, cs](TransformComponent& t,
        const WallSlideTag&              /*tag*/,
        const CharacterRadiusComponent& cr)
        {
            const float r = cr.radius;
            float px = t.position.x;
            float pz = t.position.z;

            // 4方向のサンプル点を壁判定し、めり込み量で押し出す（単純AABB押し出し）
            // X方向
            {
                int i, j;
                grid.WorldToCell(px + r, pz, i, j);
                if (grid.IsWall(i, j))
                {
                    // セル左端 = originX + i * cs
                    float wallEdge = grid.CellCenterWorldX(i) - cs * 0.5f;
                    px = wallEdge - r;
                }
                grid.WorldToCell(px - r, pz, i, j);
                if (grid.IsWall(i, j))
                {
                    float wallEdge = grid.CellCenterWorldX(i) + cs * 0.5f;
                    px = wallEdge + r;
                }
            }
            // Z方向（押し出し後の px を使う）
            {
                int i, j;
                grid.WorldToCell(px, pz + r, i, j);
                if (grid.IsWall(i, j))
                {
                    float wallEdge = grid.CellCenterWorldZ(j) - cs * 0.5f;
                    pz = wallEdge - r;
                }
                grid.WorldToCell(px, pz - r, i, j);
                if (grid.IsWall(i, j))
                {
                    float wallEdge = grid.CellCenterWorldZ(j) + cs * 0.5f;
                    pz = wallEdge + r;
                }
            }

            t.position.x = px;
            t.position.z = pz;
            t.isDirty = true;
        });
}

// CharacterVirtual 移動(L02前半)の直後に実行
REGISTER_LOGIC_SYSTEM(CharacterWallSlideSystem, Priority::LogicStage::L02_Update);