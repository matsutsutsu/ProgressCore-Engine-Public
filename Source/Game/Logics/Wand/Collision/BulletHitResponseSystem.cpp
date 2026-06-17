// ===========================================================================
// Bullet/BulletHitResponseSystem.cpp
// ----------------------------------
// @file
// @brief Jolt が出した JoltCollisionEvent を購読し、「弾 × 敵」の命中を処理する。
//        ダメージ適用と破棄フラグ(hitEnemy)立てを行う。実際の破棄/継承は
//        既存の BulletInheritanceSystem(L07) が hitEnemy を見て行う。
//
// @note 弾の移動・壁判定は従来通り(ECS駆動 / DDA)。本System は Jolt 由来の
//       敵命中だけを担当する。貫通(Focus)中の弾は破棄せず通過させる。
// ===========================================================================
#include "Game/Logics/Wand/Collision/BulletHitResponseSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Game/Logics/Wand/Bullet/BulletComponents.h"
#include "Game/Logics/Wand/Compiler/WandProgramRegistry.h"
#include "Game/Logics/Wand/Compiler/CompiledWandProgram.h"
#include "Game/Logics/Combat/HealthComponent.h"
#include "Engine/Platform/Core/Logger.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

namespace
{
    /// @brief このプログラムが貫通(Focus)を持つか。
    bool HasPierce(const CompiledWandProgram& prog)
    {
        for (uint8_t i = 0; i < prog.onHitCount; ++i)
            if (prog.onHitOps[i].type == EffectType::Focus) return true;
        return false;
    }

    /// @brief 1件の命中イベントを「弾→敵」として解決する。
    void ResolveBulletHit(Core::World* world, const WandProgramRegistry& registry,
        EntityID a, EntityID b)
    {
        // 弾側 = BulletTag を持つ方。敵側 = もう一方。
        EntityID bulletId = InvalidEntityID;
        EntityID enemyId = InvalidEntityID;

        if (world->HasComponent<BulletTag>(a)) { bulletId = a; enemyId = b; }
        else if (world->HasComponent<BulletTag>(b)) { bulletId = b; enemyId = a; }
        else return; // 弾が絡まない衝突は管轄外

        auto* st = world->GetComponent<BulletStateComponent>(bulletId);
        if (!st) return;
        if (st->hitWall || st->hitEnemy)  return; // 既に死亡予約済み
        if (enemyId == st->owner)         return; // 撃ち主は無視（保険）
        if (enemyId == st->lastHitTarget) return; // 多重ヒット防止

        // プログラムからダメージ・貫通を取得
        float baseDamage = 10.0f;
        bool  pierce = false;
        if (auto* ref = world->GetComponent<BulletProgramRef>(bulletId))
        {
            if (registry.IsValid(ref->programId))
            {
                const CompiledWandProgram& prog = registry.Get(ref->programId);
                baseDamage = prog.baseDamage;
                pierce = HasPierce(prog);
            }
        }

        // ダメージ適用（敵が HealthComponent を持つ場合）
        if (HealthComponent* hp = world->GetComponent<HealthComponent>(enemyId))
        {
            hp->currentHealth -= baseDamage;
            CCL_LOG_INFO(LogCategory::Game, "命中 enemy=%u dmg=%.1f hp=%.1f",
                enemyId, baseDamage, hp->currentHealth);
        }

        st->lastHitTarget = enemyId;
        if (!pierce) st->hitEnemy = true; // L07(BulletInheritanceSystem) が破棄/継承する
    }
}

void BulletHitResponseSystem::Initialize()
{
    // JoltCollisionEvent を購読（発行は JoltCollisionEventSystem(L05)）。
    // 配送は Publish 内で同期的に走るため、コールバック内で直接ECSを触ってよい。
    _world->GetEventBus().Subscribe<JoltCollisionEvent>(
        [this](const JoltCollisionEvent& ev)
        {
            if (!_world->HasResource<WandProgramRegistry>()) return;
            const WandProgramRegistry& registry = _world->GetResource<WandProgramRegistry>();
            ResolveBulletHit(_world, registry, ev.entityA, ev.entityB);
        });
}

void BulletHitResponseSystem::Update(float /*dt*/)
{
    // 処理はイベントコールバック内で完結（JoltCollisionEventSystem の Publish で駆動）。
}

REGISTER_LOGIC_SYSTEM(BulletHitResponseSystem, Priority::LogicStage::L05_Collision);