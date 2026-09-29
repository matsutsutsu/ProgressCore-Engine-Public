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
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 当たり判定の調査用ログ

#include <cstdio>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    // target にもう当てたか（多段ヒットを防ぐ）。最大8個なので線形に探す
    bool AlreadyHit(const HitboxComponent &box, EntityID target)
    {
        for (uint8_t i = 0; i < box.hitCount; ++i)
            if (box.hitTargets[i] == target)
                return true;
        return false;
    }

#if HITBOX_DIAGNOSTICS
    // hitTargets を "12,34," の形の文字列にする（調査用ログ）。
    // 多段ヒットの履歴が前の攻撃から残っていないかを見るため
    void FormatHitTargets(const HitboxComponent &box, char *out, size_t outSize)
    {
        size_t used = 0;
        out[0]      = '\0';
        for (uint8_t i = 0; i < box.hitCount && used + 24 < outSize; ++i)
            used += static_cast<size_t>(
                std::snprintf(out + used, outSize - used, "%llu,",
                              static_cast<unsigned long long>(box.hitTargets[i])));
    }
#endif

    // hitTargets に target を足す（いっぱいなら何もしない）
    void RecordHit(HitboxComponent &box, EntityID target)
    {
        if (box.hitCount < HitboxComponent::MAX_HIT_TARGETS)
            box.hitTargets[box.hitCount++] = target;
    }
} // namespace

void HitboxCollisionSystem::Initialize()
{
    // 物理の接触を購読する。溜めるだけにし、判定は Update で行う（理由はヘッダーを参照）
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent &ev) {
        m_pendingContacts.push_back(ev);
    });
}

void HitboxCollisionSystem::Update(float /*dt*/)
{
    // A→B と B→A の両方で調べる。どちらが攻撃側かは Hitbox を持つかで決まる
    for (const JoltCollisionEvent &ev : m_pendingContacts)
    {
        CheckAndSpawnDamageEvent(ev.entityA, ev.entityB, ev.contactPosition);
        CheckAndSpawnDamageEvent(ev.entityB, ev.entityA, ev.contactPosition);
    }

    // 処理した接触は必ず捨てる。次のフレームに残すと同じ接触を2回調べてしまう
    m_pendingContacts.clear();
}

void HitboxCollisionSystem::CheckAndSpawnDamageEvent(EntityID attackerCandidate, EntityID victimCandidate,
                                                     const DirectX::XMFLOAT3 &contactPos)
{
    // 1. 攻撃側が Hitbox、受ける側が Hurtbox を持つか
    auto *hitbox  = m_world->GetComponent<HitboxComponent>(attackerCandidate);
    auto *hurtbox = m_world->GetComponent<HurtboxComponent>(victimCandidate);

    // 攻撃側ではない（2回呼ぶうちの片方）。いつものことなのでログは出さない
    if (!hitbox) return;

    // ここからは Hitbox を持つ側なので、当たらなかった理由をログに残す
    if (!hurtbox)
    {
        HITDIAG_LOG(LogCategory::Game,
                    "[HitDiag/Reject] no-hurtbox  atk=%llu vic=%llu", attackerCandidate, victimCandidate);
        return;
    }

    if (!hitbox->isActive)
    {
        HITDIAG_LOG(LogCategory::Game,
                    "[HitDiag/Reject] inactive    atk=%llu vic=%llu (判定区間が閉じている)",
                    attackerCandidate, victimCandidate);
        return;
    }

    // 自分の攻撃が自分に当たらないようにする
    if (hitbox->ownerID == victimCandidate)
    {
        HITDIAG_LOG(LogCategory::Game,
                    "[HitDiag/Reject] self-hit    atk=%llu vic=%llu", attackerCandidate, victimCandidate);
        return;
    }

    // 2. 1回の攻撃で同じ相手に2回当てない
    if (AlreadyHit(*hitbox, victimCandidate))
    {
#if HITBOX_DIAGNOSTICS
        char targets[128];
        FormatHitTargets(*hitbox, targets, sizeof(targets));
        HITDIAG_LOG(LogCategory::Game,
                    "[HitDiag/Reject] AlreadyHit  atk=%llu vic=%llu hitCount=%u targets=[%s]",
                    attackerCandidate, victimCandidate,
                    static_cast<unsigned>(hitbox->hitCount), targets);
#endif
        return;
    }

    RecordHit(*hitbox, victimCandidate);

    // 3. ダメージ = 基本のダメージ × 受ける側の倍率（弱点など）
    const float finalDamage = hitbox->damageAmount * hurtbox->damageMultiplier;

    // 4. 当たった位置は物理の接触点（ワールド座標）をそのまま使う
    const Vector3 hitPos(contactPos);

    // 5. ノックバックの向き。攻撃の向きで決める。衝突の法線や剣先の位置で決めると、横に振った剣が
    //    相手の真横にある瞬間に当たったとき、真横へ飛んでしまう。そこで攻撃した側（剣なら持ち主）の
    //    前方を主に、攻撃した側から受けた側への向きを少し混ぜる。前方を主にすると振った向きへ飛び、
    //    中心から外れて当たったときは外側への向きが少し残る。
    //    基準の位置は ownerID（誰の攻撃か）とは限らない。剣は持ち主の子なので持ち主でよいが、
    //    ドローンは持ち主から数十 m 離れて当たるので、自分自身を基準にする（useSelfAsHitOrigin）
    const EntityID hitOrigin =
        (hitbox->useSelfAsHitOrigin || hitbox->ownerID == InvalidEntityID)
            ? attackerCandidate
            : hitbox->ownerID;

    Vector3 knockDir   = Vector3::Zero;
    float   knockForce = 0.0f;
    if (hitbox->knockback > 0.0f)
    {
        auto *atkT = m_world->GetComponent<TransformComponent>(hitOrigin);
        auto *vicT = m_world->GetComponent<TransformComponent>(victimCandidate);
        if (atkT && vicT)
        {
            // (a) 攻撃した側の前方
            Vector3 fwd(atkT->GetForward());
            fwd.y = 0.0f;
            if (fwd.LengthSquared() > 1e-6f)
                fwd.Normalize();
            else
                fwd = Vector3::Zero;

            // (b) 攻撃した側から受けた側への向き。どちらもワールド座標で比べる
            Vector3 radial = Vector3(vicT->GetWorldPosition()) - Vector3(atkT->GetWorldPosition());
            radial.y       = 0.0f;
            if (radial.LengthSquared() > 1e-6f)
                radial.Normalize();
            else
                radial = Vector3::Zero;

            // 前方 0.75、外向き 0.25 で混ぜる。
            // 判定自身が基準のとき（ドローン）は前方を混ぜない。基準が接触点のすぐそばにあるので、
            // 外向きの向きがそのまま進んできた向きになる。機体の forward 軸がモデルの前方と
            // 合っているかも気にしなくて済む
            // TODO(2026-09-06): 混ぜる割合を HitBox 区間ごとに技で指定できるようにする
            const float kForwardWeight = hitbox->useSelfAsHitOrigin ? 0.0f : 0.75f;
            Vector3     blended        = fwd * kForwardWeight + radial * (1.0f - kForwardWeight);
            if (blended.LengthSquared() > 1e-6f)
            {
                blended.Normalize();
                knockDir   = blended;
                knockForce = hitbox->knockback;
            }
            else if (!hitbox->useSelfAsHitOrigin && fwd.LengthSquared() > 1e-6f)
            {
                // 前方と外向きが打ち消し合ったまれな場合は前方を使う。
                // 判定自身が基準のときは使わない。GetForward() は +Z だがドローンのモデルは前方が -Z なので、
                // 真後ろへ飛ばしてしまう（外向きが 0 ならほぼ重なっているので、押さない方がよい）
                knockDir   = fwd;
                knockForce = hitbox->knockback;
            }
            // どちらの向きも求められないとき（真上で重なっているなど）は押さない
        }
    }

    // 6. 結果を載せた DamageEvent のエンティティを作る。HP には触らない
    static const Archetype damageArchetype = ArchetypeHelper::Generate<DamageEventComponent>();
    auto                   entityRef       = m_world->Spawn(damageArchetype);

    DamageEventComponent dmg{};
    dmg.targetID        = victimCandidate;
    dmg.attackerID      = hitbox->ownerID;
    dmg.hitOriginID     = hitOrigin;
    dmg.finalDamage     = finalDamage;
    dmg.knockbackDir    = knockDir;
    dmg.knockbackForce  = knockForce;
    dmg.hitStopDuration = hitbox->hitStopDuration;
    dmg.hitStopScale    = hitbox->hitStopScale;
    dmg.hitPosition     = hitPos;
    entityRef.Set(dmg);

    HITDIAG_LOG(LogCategory::Game,
                "[HitDiag/Hit]    %llu -> %llu  dmg=%.1f",
                attackerCandidate, victimCandidate, finalDamage);
}

REGISTER_LOGIC_SYSTEM(HitboxCollisionSystem, Priority::LogicStage::L05_Collision);
