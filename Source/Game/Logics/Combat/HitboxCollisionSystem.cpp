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
#include "Engine/Physics/JoltIntegration/HitDiagnostics.h" // 計測ログ(暫定)

#include <cstdio>

using namespace CCL::ECS;
using DirectX::SimpleMath::Vector3;

namespace
{
    // hitTargets に target が既に記録済みか(多段ヒット防止)。
    bool AlreadyHit(const HitboxComponent &box, EntityID target)
    {
        for (uint8_t i = 0; i < box.hitCount; ++i)
            if (box.hitTargets[i] == target)
                return true;
        return false;
    }

#if HITBOX_DIAGNOSTICS
    // hitTargets の中身を "12,34," 形式に整形する（計測ログ用）。
    //
    // 多段ヒット履歴が前の攻撃から残っていないかを目視するために出す。
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

    // hitTargets に target を追記する(満杯なら何もしない)。
    void RecordHit(HitboxComponent &box, EntityID target)
    {
        if (box.hitCount < HitboxComponent::MAX_HIT_TARGETS)
            box.hitTargets[box.hitCount++] = target;
    }
} // namespace

void HitboxCollisionSystem::Initialize()
{
    // 物理衝突(Jolt)の報告を購読する。
    // EventBus は同期配送なので、ここで判定まで済ませると処理が発行元
    // (JoltCollisionEventSystem)のタスク上で走り、本システムが宣言した Read/Write が
    // スケジューラから見えなくなる。積むだけに留め、判定は自分の Update で行う。
    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent &ev) {
        m_pendingContacts.push_back(ev);
    });
}

void HitboxCollisionSystem::Update(float /*dt*/)
{
    // 双方向(A→B, B→A)で命中判定する。どちらが攻撃側かは Hitbox の有無で決まる。
    for (const JoltCollisionEvent &ev : m_pendingContacts)
    {
        CheckAndSpawnDamageEvent(ev.entityA, ev.entityB, ev.contactPosition);
        CheckAndSpawnDamageEvent(ev.entityB, ev.entityA, ev.contactPosition);
    }

    // 消費済みは必ず捨てる。次フレームへ持ち越すと同じ接触を二重に評価してしまう。
    m_pendingContacts.clear();
}

void HitboxCollisionSystem::CheckAndSpawnDamageEvent(EntityID attackerCandidate, EntityID victimCandidate,
    const DirectX::XMFLOAT3 &contactPos)
{
    // 1. Hitbox(攻撃) と Hurtbox(被弾) の成立確認
    auto *hitbox  = m_world->GetComponent<HitboxComponent>(attackerCandidate);
    auto *hurtbox = m_world->GetComponent<HurtboxComponent>(victimCandidate);

    // 攻撃側ですらない＝双方向呼び出しの片割れ。正常なので無言で戻る。
    if (!hitbox) return;

    // ここから先は「Hitbox を持つ側からの呼び出し」なので、落ちた理由を必ず残す。
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

    // 自傷防止（owner と被弾者が同じなら無視）
    if (hitbox->ownerID == victimCandidate)
    {
        HITDIAG_LOG(LogCategory::Game,
            "[HitDiag/Reject] self-hit    atk=%llu vic=%llu", attackerCandidate, victimCandidate);
        return;
    }

    // 2. 多段ヒット防止(同一攻撃が同一相手を二度叩かない)
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

    // 当たった相手を記録しておく（多重ヒット防止のため）
    RecordHit(*hitbox, victimCandidate);

    // 3. 最終ダメージ = 基礎 × 被ダメ倍率(弱点)
    //    ※1対1ボス戦のためクリティカルは無し。
    const float finalDamage = hitbox->damageAmount * hurtbox->damageMultiplier;

    // 4. 命中位置：物理衝突の接触点(ワールド座標)をそのまま使う。加工しない。
    const Vector3 hitPos(contactPos);


    // 5. ノックバック方向
    //    定石（DMC/ベヨネッタ/GoW系）に倣い「攻撃が方向を決める」。衝突法線や剣先の一瞬の
    //    位置を基準にすると、横薙ぎで相手の真横に剣がある瞬間に当たり“真横へ吹っ飛ぶ”違和感が
    //    出る。そこで【攻撃者(キャラ本体)の前方】を主軸に、【攻撃者→被弾者の放射方向】を従で
    //    ブレンドする。前方主軸で「振った向き＝前へ」飛ばしつつ、中心を外した被弾では放射成分で
    //    自然なズレが残る。※剣の場合の基準は剣ボディではなく攻撃の持ち主(ownerID)。
    //
    // 幾何の原点。ownerID は「誰の手柄か」であって「どこから来たか」ではない。
    //   剣は持ち主の子なので owner でよいが、ドローンは持ち主から数十m離れた
    //   攻撃体なので自分自身を原点にする（useSelfAsHitOrigin）。
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
            // (a) 攻撃者の前方（技が意図する主方向）
            Vector3 fwd(atkT->GetForward());
            fwd.y = 0.0f;
            if (fwd.LengthSquared() > 1e-6f) fwd.Normalize();
            else                             fwd = Vector3::Zero;

            // (b) 放射方向（攻撃者→被弾者）。GetWorldPosition() 基準で空間を揃える。
            Vector3 radial = Vector3(vicT->GetWorldPosition()) - Vector3(atkT->GetWorldPosition());
            radial.y = 0.0f;
            if (radial.LengthSquared() > 1e-6f) radial.Normalize();
            else                                radial = Vector3::Zero;

            // 前方0.75 : 放射0.25 でブレンド（将来はHitBoxイベント側で技ごとに指定できるとよい）。
            //   攻撃体自身が原点のとき(ドローン)は前方を混ぜない。原点が接触点の
            //   すぐそばに在るので放射方向がそのまま進行方向になり、かつ機体の
            //   forward 軸がモデルの前方と一致している保証も要らなくなる。
            const float kForwardWeight = hitbox->useSelfAsHitOrigin ? 0.0f : 0.75f;
            Vector3 blended = fwd * kForwardWeight + radial * (1.0f - kForwardWeight);
            if (blended.LengthSquared() > 1e-6f)
            {
                blended.Normalize();
                knockDir   = blended;
                knockForce = hitbox->knockback;
            }
            else if (!hitbox->useSelfAsHitOrigin && fwd.LengthSquared() > 1e-6f)
            {
                // 前方と放射が打ち消し合った稀なケースは前方で代替。
                //   攻撃体自身が原点のときは代替しない。GetForward() は +Z 基準だが
                //   ドローンのモデルは前方が -Z なので、ここで拾うと真後ろへ飛ばす。
                //   （放射がゼロ＝ほぼ完全に重なっている状況なので、
                //     knockForce=0 のまま押さない方が安全側）
                knockDir   = fwd;
                knockForce = hitbox->knockback;
            }
            // どちらも無効（真上重なり等）は knockForce=0 のまま(安全側)
        }
    }

    // 5. DamageEvent(一時エンティティ)を生成して結果を載せる。HPは触らない。
    static const Archetype damageArchetype = ArchetypeHelper::Generate<DamageEventComponent>();
    auto                   entityRef       = m_world->Spawn(damageArchetype);

    DamageEventComponent dmg{};
    dmg.targetID       = victimCandidate;
    dmg.attackerID     = hitbox->ownerID;
    dmg.hitOriginID    = hitOrigin;
    dmg.finalDamage    = finalDamage;
    dmg.knockbackDir   = knockDir;
    dmg.knockbackForce = knockForce;
    dmg.hitStopDuration = hitbox->hitStopDuration;
    dmg.hitStopScale    = hitbox->hitStopScale;
    dmg.hitPosition     = hitPos;
    entityRef.Set(dmg);

    HITDIAG_LOG(LogCategory::Game,
        "[HitDiag/Hit]    %llu -> %llu  dmg=%.1f",
        attackerCandidate, victimCandidate, finalDamage);
}

REGISTER_LOGIC_SYSTEM(HitboxCollisionSystem, Priority::LogicStage::L05_Collision);
