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
//   - EventBus のコールバックでは「受け取って積む」だけにし、判定は自分の Update で行う。
//     EventBus は同期配送なので、コールバックの中で判定すると処理が発行元
//     (JoltCollisionEventSystem)のタスク上で走ってしまう。そうなると本システムが
//     宣言した Read/Write 型はスケジューラから見えず、HitboxComponent を書く
//     CombatAnimationSyncSystem と同じバッチに入って並列実行される＝データ競合になる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/CombatComponents.h"

#include <DirectXMath.h>
#include <vector>

class HitboxCollisionSystem
    : public CCL::ECS::IfSystem<HitboxCollisionSystem,
                                CCL::ECS::Write<HitboxComponent>,   // 多段ヒット履歴(hitTargets/hitCount)を書く
                                CCL::ECS::Read<HurtboxComponent>,   // 被ダメ倍率を読む
                                CCL::ECS::Read<TransformComponent>> // ノックバック方向の算出に読む
{
public:
    HitboxCollisionSystem() : IfSystem("HitboxCollisionSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // 判定に必要な入力が揃ってから走らせる。全て同じ L05_Collision のため、
    // 明示宣言しないと順序が不定になる
    // （型が同じでなくても宣言があれば TaskScheduler が直列化する）。
    //
    // JoltCollisionEventSystem: これより前だとその回の接触を丸ごと取りこぼす。
    //
    // CombatAnimationSyncSystem: これより前だと isActive が1フレーム古くなる。
    //
    // JoltPullSystem: ノックバック方向に使う Transform を物理結果の書き戻し後に
    // 読むため。現状は剣(Kinematic)もキャラ(CharacterVirtual)も Pull 対象外
    // なので実害は無いが、将来 Dynamic な剛体に Hurtbox を付けたときに
    // 古い座標で吹き飛び方向を出さないよう向きを固定しておく。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem", "CombatAnimationSyncSystem", "JoltPullSystem"};
    }

private:
    /**
     * @brief attacker候補→victim候補の一方向で命中を検証し、成立すればDamageEventを生成する。
     * @param attackerCandidate 攻撃側になり得るエンティティ(Hitbox所持を確認する)
     * @param victimCandidate   被弾側になり得るエンティティ(Hurtbox所持を確認する)
     * @param contactPos        物理衝突の接触点(ワールド座標)。命中エフェクト位置にそのまま使う。
     * @note  双方向(A→B, B→A)は呼び出し側で2回呼ぶことで担保する。
     */
    void CheckAndSpawnDamageEvent(CCL::ECS::EntityID       attackerCandidate,
                                  CCL::ECS::EntityID       victimCandidate,
                                  const DirectX::XMFLOAT3 &contactPos);

    // このフレームに届いた接触。コールバックが積み、Update が消費して空にする。
    //
    // 発行元は JoltCollisionEventSystem 1つだけで、GetRunAfter により本システムと
    // 直列化されるため、積む側と消費する側が同時に走ることはない。
    std::vector<JoltCollisionEvent> m_pendingContacts;
};