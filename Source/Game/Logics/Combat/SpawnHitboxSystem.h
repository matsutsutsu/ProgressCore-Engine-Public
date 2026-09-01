#pragma once
// ===========================================================================
// SpawnHitboxSystem.h
// -------------------
// 役割:
//   アニメーションの点イベント AnimEventSpawnHitboxMessage を購読し、
//   「空間に独立した球状ヒットボックス」を実体エンティティとして生成する。
//
// 責務:
//   - owner の Transform を基準に、offset をワールド回転で回して生成位置を確定。
//   - 衝突レイヤーはイベントの attackLayer で明示指定（PLAYER_ATTACK/BOSS_ATTACK）。
//   - Transform + Hitbox + Jolt(sensor) + SphereCollider + HitboxLifetime を載せて Spawn。
//     以降は既存パイプラインに完全委譲する:
//       JoltBodySetupSystem(L03) が sensor 剛体を生成
//         → 物理衝突 → HitboxCollisionSystem(L05) が命中判定 → DamageEvent
//         → 本システムの Update が寿命で Destroy → JoltCleanupSystem が剛体回収
//
// アーキテクチャ上の位置付け:
//   剣など「既に存在する子コライダを区間で ON/OFF する」経路
//   (CombatAnimationSyncSystem) の対になる、「攻撃の瞬間に判定を生成する」経路。
//   生成した Hitbox は ownerEntity=Invalid のため CombatAnimationSyncSystem は
//   一切触れず、寿命が尽きるまで常時アクティブな弾扱いとなる。
//
// 設計方針:
//   - 生成は遅延構造変更(PendingOps)。ScrutinyAndApply はフレームワークに委ねる。
//   - 寿命管理は自前。生成した Hitbox に HitboxLifetimeComponent を付け、Update で
//     remainingLife を減算し 0 以下で Destroy する(汎用のタイマー機構は持たない)。
//     Write は自身が生成したタグ型のみ宣言し、剣 Hitbox を駆動する
//     CombatAnimationSyncSystem(Write<HitboxComponent>) とは競合しない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/CombatComponents.h" // HitboxLifetimeComponent

struct AnimEventSpawnHitboxMessage; // 前方宣言

// アニメ由来の球状ヒットボックス生成と、その寿命管理を担うシステム。
class SpawnHitboxSystem
    : public CCL::ECS::IfSystem<SpawnHitboxSystem,
          CCL::ECS::Write<HitboxLifetimeComponent>>
{
public:
    SpawnHitboxSystem() : IfSystem("SpawnHitboxSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    /**
     * @brief 1通の生成依頼から、判定球エンティティを1つ生成する。
     * @param msg 生成パラメータ(owner/offset/radius/damage/hitStop/lifeTime)。
     * @note  owner に Transform が無い場合は基準が定まらないため生成しない(安全側)。
     */
    void OnSpawnHitbox(const AnimEventSpawnHitboxMessage& msg);
};
