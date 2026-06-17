// ===========================================================================
// Systems/BulletWallCollisionSystem.cpp
// ---------------------------------------
#include "BulletWallCollisionSystem.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/LevelGen/DungeonGrid.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;
using namespace RuneForge::Dungeon;

namespace
{
    /// @brief OnHit命令を見て「貫通するか」「反射するか」を解釈する。
    struct WallHitResult { bool pierce = false; bool reflect = false; };

    WallHitResult ApplyWallHitOps(const CompiledWandProgram& prog)
    {
        WallHitResult r;
        for (uint8_t i = 0; i < prog.onHitCount; ++i)
        {
            const EffectOp& op = prog.onHitOps[i];
            switch (op.type)
            {
            case EffectType::Focus:      r.pierce = true; break; // 貫通（壁を無視）
            case EffectType::Reflection: r.reflect = true; break; // 壁で跳ね返る
                // TODO: Accelerate（貫通時加速）は pierce が立ったあとに適用
            default: break;
            }
        }
        return r;
    }
}

void BulletWallCollisionSystem::Update(float /*dt*/)
{
    if (!_world->HasResource<DungeonGrid>())       return;
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

            // OnHit 命令を解釈
            WallHitResult r;
            if (registry.IsValid(ref.programId))
                r = ApplyWallHitOps(registry.Get(ref.programId));

            if (r.pierce) return; // 壁を無視して通過

            if (r.reflect)
            {
                // 反射: R = D - 2(D・N)N
                float dot = dirX * hit.normalX + dirZ * hit.normalZ;
                st.direction.x = dirX - 2.0f * dot * hit.normalX;
                st.direction.z = dirZ - 2.0f * dot * hit.normalZ;
                st.direction.y = 0.0f; // XZ平面限定
                st.direction.Normalize();
                // 弾を壁面手前に戻す
                // (TransformComponentは Read になっているが、Reflectionは非常に稀なので
                //  次フレームからの向き変更で事足りる。位置補正が要れば Write に変える)
            }
            else
            {
                st.hitWall = true; // L07 で BulletInheritanceSystem が破棄/継承する
            }
        });
}

REGISTER_LOGIC_SYSTEM(BulletWallCollisionSystem, Priority::LogicStage::L06_Resolution);