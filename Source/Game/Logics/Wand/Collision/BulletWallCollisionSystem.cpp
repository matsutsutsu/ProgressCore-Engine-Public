// ===========================================================================
// Systems/BulletWallCollisionSystem.cpp
// -------------------------------------
// 役割:
//   弾と壁(DungeonGrid)の命中を DDA で判定し、OnHit を解決する。貫通ならすり抜け、
//   反射なら向きを跳ね返し、それ以外は hitWall を立てて L07 で破棄させる。
//
// 実装の意図(Why):
//   - OnHit の解釈は敵命中と共通の ResolveOnHit に一本化する。壁・敵で「貫通」の意味を
//     揃え、効果追加が1箇所(ハンドラ)で済むようにする。
//   - 反射ベクトルは壁の法線が要るため、計算はこの壁System側で行う(敵命中では反射しない)。
//   - 壁判定は前フレーム位置→現在位置の線分DDA。すり抜け(トンネリング)を防ぐ。
// ===========================================================================
#include "Game/Logics/Wand/Collision/BulletWallCollisionSystem.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Game/Logics/Wand/Effects/ResolveOnHit.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include <cmath>

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

void BulletWallCollisionSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<DungeonGrid>())         return;
    if (!_world->HasResource<WandProgramRegistry>()) return;

    const DungeonGrid& grid = _world->GetResource<DungeonGrid>();
    const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();

    // per-entity 書き込みのみ（破棄なし）→ 並列OK
    ForEachParallel([&grid, &registry](
        BulletStateComponent& st,
        const TransformComponent& t,
        const BulletProgramRef& ref,
        const BulletTag&            /*tag*/)
        {
            if (st.hitWall || st.hitEnemy) return; // 既に死亡予約済み

            // 前フレーム位置→現在位置の線分をDDAで走査
            const float segX = t.position.x - st.prevPosition.x;
            const float segZ = t.position.z - st.prevPosition.z;
            const float len = std::sqrt(segX * segX + segZ * segZ);
            if (len < 1e-4f) return;

            const float dirX = segX / len;
            const float dirZ = segZ / len;

            GridRayHit hit = grid.RaycastDDA(
                st.prevPosition.x, st.prevPosition.z,
                dirX, dirZ, len + 0.1f); // 0.1f = 弾の見かけ半径

            if (!hit.hit) return;

            // OnHit を共通解決器で解釈（敵命中と同一のハンドラ群）。
            OnHitResolution r;
            if (registry.IsValid(ref.programId))
                r = ResolveOnHit(registry.Get(ref.programId));

            if (r.pierce) return; // 壁をすり抜けて通過

            if (r.reflect)
            {
                // 反射: R = D - 2(D・N)N （壁の法線が必要なのでここで計算）
                const float dot = dirX * hit.normalX + dirZ * hit.normalZ;
                st.direction.x = dirX - 2.0f * dot * hit.normalX;
                st.direction.z = dirZ - 2.0f * dot * hit.normalZ;
                st.direction.y = 0.0f; // XZ平面限定
                st.direction.Normalize();
                // 位置補正は省略（反射は稀。次フレームの向き変更で実用上十分）。
            }
            else
            {
                st.hitWall = true; // L07 BulletInheritanceSystem が破棄/継承する
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletWallCollisionSystem, Priority::LogicStage::L06_Resolution);