//// ===========================================================================
//// HitboxCollisionSystem.cpp
//// -------------------------
//// 役割・責務・パイプライン上の位置付けは HitboxCollisionSystem.h を参照。
////
//// 実装の意図(Why):
////   - 多段ヒット防止は要素数最大8の固定長配列の線形探索で行う(ヒープ確保回避・
////     キャッシュ常駐)。貫通弾(Focus)が複数の敵を順に貫く際、同一弾が同一敵を
////     二度叩かないことをこの配列で保証する。
////   - クリ判定の乱数は GameRng(World Resource)を使う。System内で乱数器を持たず
////     共有の1インスタンスから引くことで、乱数列の決定性(シード再現)を保つ。
////   - ノックバック方向は「攻撃者→被弾者」の位置差から求める。弾の場合、攻撃者は
////     弾自身(Hitbox所持エンティティ)なので、弾の位置→敵の位置が吹っ飛び方向になる。
//// ===========================================================================
//#include "HitboxCollisionSystem.h"
//#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
//#include "CombatComponents.h"
//#include "Engine/Transform/Components/TransformComponent.h"
//#include "Engine/ECS/Core/CCL_World.h"
//#include "Engine/ECS/Core/CCL_PendingOps.h"
//#include "Engine/ECS/Common/CCL_Archetype.h"
//#include "Engine/ECS/System/CCL_SystemRegistry.h"
//#include "Engine/Core/Systems/SystemPriority.h"
//#include "Engine/Platform/Core/Logger.h"
//
//using namespace CCL::ECS;
//using DirectX::SimpleMath::Vector3;
//
//namespace
//{
//    /// @brief hitTargets に target が既に記録済みか(多段ヒット防止)。
//    bool AlreadyHit(const HitboxComponent& box, EntityID target)
//    {
//        for (uint8_t i = 0; i < box.hitCount; ++i)
//            if (box.hitTargets[i] == target) return true;
//        return false;
//    }
//
//    /// @brief hitTargets に target を追記する(満杯なら何もしない)。
//    void RecordHit(HitboxComponent& box, EntityID target)
//    {
//        if (box.hitCount < HitboxComponent::MAX_HIT_TARGETS)
//            box.hitTargets[box.hitCount++] = target;
//    }
//}
//
//void HitboxCollisionSystem::Initialize() {
//    // EventBusから物理衝突の報告を購読する(双方向で判定)。
//    ListenEvent<JoltCollisionEvent>([this](const JoltCollisionEvent& ev) {
//        CheckAndSpawnDamageEvent(ev.entityA, ev.entityB);
//        CheckAndSpawnDamageEvent(ev.entityB, ev.entityA);
//        });
//
//    CCL_LOG_INFO(LogCategory::Game, "HitboxCollisionSystem Initialized.");
//}
//
//void HitboxCollisionSystem::Update(float dt) {
//    // イベント駆動のためUpdateは空(EventBusは同期配送)。
//}
//
//void HitboxCollisionSystem::CheckAndSpawnDamageEvent(EntityID attackerCandidate, EntityID victimCandidate)
//{
//    // 1. Hitbox(攻撃) と Hurtbox(被弾) の成立確認
//    auto* hitbox = _world->GetComponent<HitboxComponent>(attackerCandidate);
//    auto* hurtbox = _world->GetComponent<HurtboxComponent>(victimCandidate);
//    if (!hitbox || !hitbox->isActive || !hurtbox) return;
//
//    // 2. 多段ヒット防止(同一攻撃が同一相手を二度叩かない)
//    if (AlreadyHit(*hitbox, victimCandidate)) return;
//    RecordHit(*hitbox, victimCandidate);
//
//    // 3. クリティカル判定(GameRng)
//    bool  isCritical = false;
//    float critMult = 1.0f;
//    if (hitbox->critRate > 0.0f && _world->HasResource<GameRng>())
//    {
//        GameRng& rng = _world->GetResource<GameRng>();
//        if (rng.Chance(hitbox->critRate))
//        {
//            isCritical = true;
//            critMult = hitbox->critMult;
//        }
//    }
//
//    // 4. 最終ダメージ = 基礎 × 被ダメ倍率(弱点) × クリ倍率
//    const float finalDamage = hitbox->damageAmount * hurtbox->damageMultiplier * critMult;
//
//    // 5. ノックバック方向(攻撃者→被弾者のXZ方向)を算出
//    Vector3 knockDir = Vector3::Zero;
//    float   knockForce = 0.0f;
//    if (hitbox->knockback > 0.0f)
//    {
//        auto* atkT = _world->GetComponent<TransformComponent>(attackerCandidate);
//        auto* vicT = _world->GetComponent<TransformComponent>(victimCandidate);
//        if (atkT && vicT)
//        {
//            // 取得した XMFLOAT3 を Vector3 に変換してから引き算を行う
//            Vector3 d = Vector3(vicT->position) - Vector3(atkT->position);
//            d.y = 0.0f; // 平面ステージ。吹っ飛びはXZのみ
//            if (d.LengthSquared() > 1e-6f)
//            {
//                d.Normalize();
//                knockDir = d;
//                knockForce = hitbox->knockback;
//            }
//        }
//        // 位置が取れない/真上重なり時は knockForce=0 のまま(無効・安全側)
//    }
//
//    // 6. DamageEvent(一時エンティティ)を生成して結果を載せる。HPは触らない。
//    static const Archetype damageArchetype = ArchetypeHelper::Generate<DamageEventComponent>();
//    auto entityRef = _world->Spawn(damageArchetype);
//
//    DamageEventComponent ev{};
//    ev.targetID = victimCandidate;
//    ev.attackerID = hitbox->ownerID;
//    ev.finalDamage = finalDamage;
//    ev.isCritical = isCritical;
//    ev.knockbackDir = knockDir;
//    ev.knockbackForce = knockForce;
//    entityRef.Set(ev);
//}
//
//REGISTER_LOGIC_SYSTEM(HitboxCollisionSystem, Priority::LogicStage::L05_Collision);