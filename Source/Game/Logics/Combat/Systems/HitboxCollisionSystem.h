#pragma once
// ===========================================================================
// 概要: 物理の接触（JoltCollisionEvent）から、攻撃判定と被弾判定が当たったかを調べ、
//       命中の結果（DamageEventComponent）を作る System。
//
// 責務:
//   - 攻撃側が Hitbox・受ける側が Hurtbox を持ち、判定が開いていて、自分でも同じ相手でもないかを確かめる
//   - ダメージ（基本のダメージ × 受ける側の倍率）とノックバックの向きを決める
//   - 結果を一時的なエンティティに載せる（HP や無敵時間は変えない）
//
// 備考: 戦闘の流れ:
//         L05 HitboxCollisionSystem  … この System
//         L06 DamageResolutionSystem … HP を減らしてノックバックを書く
//         L06 HealthSystem           … 無敵時間を減らし、倒れたかを判定する
//       イベントのコールバックでは受け取って溜めるだけにし、判定は Update で行う。EventBus は
//       その場で呼び出すので、コールバックの中で判定すると発行元（JoltCollisionEventSystem）の
//       タスクの上で走る。するとこの System が申告した Read / Write がスケジューラに伝わらず、
//       HitboxComponent を書く CombatAnimationSyncSystem と同時に走ってデータが競合する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <DirectXMath.h>
#include <vector>

class HitboxCollisionSystem
    : public CCL::ECS::IfSystem<HitboxCollisionSystem,
                                CCL::ECS::Write<HitboxComponent>,   // 多段ヒットの履歴（hitTargets / hitCount）を書く
                                CCL::ECS::Read<HurtboxComponent>,   // 受けるダメージの倍率を読む
                                CCL::ECS::Read<HealthComponent>,    // 無敵中の相手を履歴に残さないため読む
                                CCL::ECS::Read<TransformComponent>> // ノックバック方向の算出に読む
{
public:
    HitboxCollisionSystem() : IfSystem("HitboxCollisionSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    /**
     * @brief 判定に使う入力がそろってから走らせる。
     *
     * どれも同じ L05_Collision なので、書かないと順番が決まらない（型が重ならなくても、書けば TaskScheduler が順番に走らせる）。
     *   JoltCollisionEventSystem : これより前だと、そのフレームの接触を全部取りこぼす
     *   CombatAnimationSyncSystem: これより前だと、isActive が1フレーム古くなる
     *   JoltPullSystem           : ノックバックの向きに使う Transform を、物理の結果を書き戻した後に読むため。
     *                              今は剣（Kinematic）もキャラ（CharacterVirtual）も書き戻しの対象外だが、
     *                              Dynamic な剛体に Hurtbox を付けたときに古い位置で向きを決めないようにする
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem", "CombatAnimationSyncSystem", "JoltPullSystem"};
    }

private:
    /**
     * @brief attackerCandidate が victimCandidate に当てたかを調べ、当たっていれば DamageEvent を作る。
     * @param attackerCandidate 攻撃側かもしれないエンティティ（Hitbox を持つかを確かめる）
     * @param victimCandidate   受ける側かもしれないエンティティ（Hurtbox を持つかを確かめる）
     * @param contactPos        物理の接触点（ワールド座標）。火花を出す位置にそのまま使う
     * @note  どちらが攻撃側か分からないので、呼び出し側が A→B と B→A の2回呼ぶ。
     */
    void CheckAndSpawnDamageEvent(CCL::ECS::EntityID       attackerCandidate,
                                  CCL::ECS::EntityID       victimCandidate,
                                  const DirectX::XMFLOAT3 &contactPos);

    // このフレームに届いた接触。コールバックが追加し、Update が処理して空にする。
    // 発行元は JoltCollisionEventSystem だけで、GetRunAfter でこの System の前に走るので、
    // 追加と処理が同時に走ることはない
    std::vector<JoltCollisionEvent> m_pendingContacts;

    /// 再ヒット間隔を測る時計（秒）。HitboxComponent::hitTimes はこの値で記録する。
    /// 間隔が要る判定だけ毎フレーム経過時間を数える方法だと全 Hitbox を回すことになるので、1本の時計の差で測る
    float m_clock = 0.0f;
};