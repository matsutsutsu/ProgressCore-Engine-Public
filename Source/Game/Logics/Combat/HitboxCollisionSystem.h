#pragma once
// ===========================================================================
// HitboxCollisionSystem.h
// -----------------------
// 役割:
//   物理衝突(JoltCollisionEvent)を受け、Hitbox×Hurtbox の交差から
//   ダメージイベント(DamageEventComponent)を生成する「判定」システム。
//
// 責務:
//   - 攻撃側Hitboxと被弾側Hurtboxの成立確認、多段ヒット防止。
//   - クリティカル判定(CombatRng)と最終ダメージ・ノックバック方向の確定。
//   - 結果を DamageEvent として一時エンティティに載せるところまで(HPは触らない)。
//
// アーキテクチャ上の位置付け(戦闘パイプライン):
//   [Jolt] 物理衝突 → EventBus
//     │
//   L05 HitboxCollisionSystem  ← 本ファイル(判定・イベント生成)
//     ▼
//   L06 DamageResolutionSystem (HP減算＋ノックバック付与)
//     ▼
//   L06 HealthSystem           (無敵更新・死亡判定)
//
// 設計方針:
//   - 状態(HP・無敵)は一切変更しない。出来事(Event)をデータとして並べるのみ(DOD)。
//   - クリ乱数は CombatRng(World Resource)を使い、DungeonRng とは分離する。
//   - ノックバック方向は攻撃者→被弾者の Transform から算出(XZ平面)。位置が取れない
//     場合は強さ0として無効化する(安全側)。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include <DirectXMath.h>

class HitboxCollisionSystem : public CCL::ECS::IfSystem<HitboxCollisionSystem> {
public:
    HitboxCollisionSystem() : IfSystem("HitboxCollisionSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    /// @brief attacker候補→victim候補の一方向で命中を検証し、成立すればDamageEventを生成する。
    /// @param attackerCandidate 攻撃側になり得るエンティティ(Hitbox所持を確認する)
    /// @param victimCandidate   被弾側になり得るエンティティ(Hurtbox所持を確認する)
    /// @param contactPos        物理衝突の接触点(ワールド座標)。命中エフェクト位置にそのまま使う。
    /// @note  双方向(A→B, B→A)は呼び出し側で2回呼ぶことで担保する。
    void CheckAndSpawnDamageEvent(CCL::ECS::EntityID attackerCandidate,
        CCL::ECS::EntityID victimCandidate,
        const DirectX::XMFLOAT3 &contactPos);
};