// ===========================================================================
// HitboxCollisionSystem.cpp
// 役割・責務・パイプライン上の位置付けは HitboxCollisionSystem.h を参照。
//
// 実装の意図(Why):
//   - 多段ヒット防止は最大8要素の固定長配列の線形探索(ヒープ確保回避・キャッシュ常駐)。
//   - ノックバック方向は「攻撃者→被弾者」の位置差(XZ)から算出する。
//   - 1対1アクションボス戦のためクリティカル判定は持たない。
// ===========================================================================
#include "HitboxCollisionSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "CombatComponents.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/Core/CCL_PendingOps.h"
#include "Engine/ECS/Common/CCL_Archetype.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Platform/Core/Logger.h"

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
/// @brief hitTargets に target が既に記録済みか(多段ヒット防止)。
bool AlreadyHit(const HitboxComponent &box, EntityID target)
{
    for (uint8_t i = 0; i < box.hitCount; ++i)
        if (box.hitTargets[i] == target)
            return true;
    return false;
}

/// @brief hitTargets に target を追記する(満杯なら何もしない)。
void RecordHit(HitboxComponent &box, EntityID target)
{
    if (box.hitCount < HitboxComponent::MAX_HIT_TARGETS)
        box.hitTargets[box.hitCount++] = target;
}
} // namespace

void HitboxCollisionSystem::Initialize()
{
    // 物理衝突(Jolt)の報告を購読し、双方向(A→B, B→A)で命中判定する。
    // EventBus は同期配送のため、コールバックはこの Initialize 完了後の
    // 物理ステージ内で呼ばれる。
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent &ev) {
        CheckAndSpawnDamageEvent(ev.entityA, ev.entityB, ev.contactPosition);
        CheckAndSpawnDamageEvent(ev.entityB, ev.entityA, ev.contactPosition);
    });

    CCL_LOG_INFO(LogCategory::Game, "HitboxCollisionSystem Initialized.");
}

void HitboxCollisionSystem::Update(float /*dt*/)
{
    // イベント駆動のため Update は空。判定は ListenEvent コールバックで行う。
}

void HitboxCollisionSystem::CheckAndSpawnDamageEvent(EntityID attackerCandidate, EntityID victimCandidate,
    const DirectX::XMFLOAT3 &contactPos)
{
    // 1. Hitbox(攻撃) と Hurtbox(被弾) の成立確認
    auto *hitbox  = _world->GetComponent<HitboxComponent>(attackerCandidate);
    auto *hurtbox = _world->GetComponent<HurtboxComponent>(victimCandidate);
    if (!hitbox || !hitbox->isActive || !hurtbox)
        return;

    // 自傷防止（owner と被弾者が同じなら無視）
    if (hitbox->ownerID == victimCandidate)
        return;

    // 2. 多段ヒット防止(同一攻撃が同一相手を二度叩かない)
    if (AlreadyHit(*hitbox, victimCandidate))
        return;
    RecordHit(*hitbox, victimCandidate);

    // 3. 最終ダメージ = 基礎 × 被ダメ倍率(弱点)
    //    ※1対1ボス戦のためクリティカルは無し。
    const float finalDamage = hitbox->damageAmount * hurtbox->damageMultiplier;

    // 4. 命中位置：物理衝突の接触点(ワールド座標)をそのまま使う。加工しない。
    const Vector3 hitPos(contactPos);

    // 5. ノックバック方向(攻撃者→被弾者のXZ方向)
    Vector3 knockDir   = Vector3::Zero;
    float   knockForce = 0.0f;
    if (hitbox->knockback > 0.0f)
    {
        auto *atkT = _world->GetComponent<TransformComponent>(attackerCandidate);
        auto *vicT = _world->GetComponent<TransformComponent>(victimCandidate);
        if (atkT && vicT)
        {
            Vector3 d = Vector3(vicT->position) - Vector3(atkT->position);
            d.y       = 0.0f; // 平面ステージ。吹っ飛びはXZのみ
            if (d.LengthSquared() > 1e-6f)
            {
                d.Normalize();
                knockDir   = d;
                knockForce = hitbox->knockback;
            }
        }
        // 位置が取れない/真上重なり時は knockForce=0 のまま(安全側)
    }

    // 5. DamageEvent(一時エンティティ)を生成して結果を載せる。HPは触らない。
    static const Archetype damageArchetype = ArchetypeHelper::Generate<DamageEventComponent>();
    auto                   entityRef       = _world->Spawn(damageArchetype);

    DamageEventComponent dmg{};
    dmg.targetID       = victimCandidate;
    dmg.attackerID     = hitbox->ownerID;
    dmg.finalDamage    = finalDamage;
    dmg.knockbackDir   = knockDir;
    dmg.knockbackForce = knockForce;
    dmg.hitStopDuration = hitbox->hitStopDuration;
    dmg.hitStopScale    = hitbox->hitStopScale;
    dmg.hitPosition     = hitPos;
    entityRef.Set(dmg);

    CCL_LOG_INFO(LogCategory::Game,
        "[Hitbox] %llu -> %llu  dmg=%.1f",
        attackerCandidate, victimCandidate, finalDamage);
}

REGISTER_LOGIC_SYSTEM(HitboxCollisionSystem, Priority::LogicStage::L05_Collision);