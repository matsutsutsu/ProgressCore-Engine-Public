// ===========================================================================
// BulletEnemyCollisionSystem.cpp
// ------------------------------
// 役割・責務・パイプライン上の位置付けはヘッダを参照。
//
// 実装の意図(Why):
//   - JoltCollisionEvent には弾と敵のどちらが entityA/B か分からないので、BulletTag を
//     持つ方を弾として特定する。弾が絡まないイベントは無視(壁等は別System)。
//   - OnHit を ResolveOnHit で解釈し、pierce(貫通)でなければ hitEnemy=true。これにより
//     L07 BulletInheritanceSystem が弾を破棄する。貫通弾は消えず飛び続ける。
//   - ダメージは出さない(HitboxCollisionSystem の責務)。ここは弾の生死だけを扱う。
//   - 既に hitWall/hitEnemy が立っている弾は二重処理しない。
// ===========================================================================
#include "BulletEnemyCollisionSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Game/Logics/Character/Enemy/EnemyComponent.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/Wand/Effects/ResolveOnHit.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

namespace
{
    /// @brief 1件の接触を「弾→敵」として解決する。
    void ResolveBulletEnemyHit(Core::World* world, const WandProgramRegistry& registry,
        EntityID a, EntityID b)
    {
        // 弾側 = BulletTag を持つ方。
        EntityID bulletId = InvalidEntityID;
        EntityID enemyId = InvalidEntityID;
        if (world->HasComponent<BulletTag>(a)) { bulletId = a; enemyId = b; }
        else if (world->HasComponent<BulletTag>(b)) { bulletId = b; enemyId = a; }
        else return; // 弾が絡まない接触は管轄外

        // 相手が敵か（EnemyComponent を持つか）を確認。
        if (!world->HasComponent<EnemyComponent>(enemyId)) return;

        auto* st = world->GetComponent<BulletStateComponent>(bulletId);
        if (!st) return;
        if (st->hitWall || st->hitEnemy) return; // 既に死亡予約済み

        // OnHit を解釈：貫通なら消えず、そうでなければ hitEnemy を立てる。
        bool pierce = false;
        if (auto* ref = world->GetComponent<BulletProgramRef>(bulletId))
        {
            if (registry.IsValid(ref->programId))
            {
                const OnHitResolution r = ResolveOnHit(registry.Get(ref->programId));
                pierce = r.pierce;
            }
        }

        if (!pierce)
            st->hitEnemy = true; // L07 BulletInheritanceSystem が破棄する
        // 貫通弾は何もしない（飛び続ける。多段ヒット防止は Hitbox 側 hitTargets が担保）
    }
}

void BulletEnemyCollisionSystem::Initialize()
{
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev)
        {
            if (!_world->HasResource<WandProgramRegistry>()) return;
            const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();
            ResolveBulletEnemyHit(_world, registry, ev.entityA, ev.entityB);
        });
}

void BulletEnemyCollisionSystem::Update(float /*dt*/)
{
    // イベント駆動のため Update は空（EventBus は同期配送）。
}

REGISTER_LOGIC_SYSTEM(BulletEnemyCollisionSystem, Priority::LogicStage::L06_Resolution);