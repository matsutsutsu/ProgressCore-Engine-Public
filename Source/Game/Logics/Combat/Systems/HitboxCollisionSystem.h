#pragma once
// ===========================================================================
// 概要: 物理の接触（JoltCollisionEvent）から、攻撃判定と被弾判定が当たったかを調べ、
//       命中の結果（DamageEventComponent）を作る System
//
// 責務:
//   - 攻撃側が Hitbox・受ける側が Hurtbox を持ち、判定が開いていて、自分でも同じ相手でもないかを確かめる
//   - ダメージ（基本のダメージ × 受ける側の倍率）とノックバックの向きを決める
//   - 結果を一時的なエンティティに載せる（HP や無敵時間は変えない）
//
// 備考: 戦闘の流れは L05 HitboxCollisionSystem → L06 DamageResolutionSystem → L06 HealthSystem
//       コールバックでは接触を溜めるだけにし、判定は Update で行う
//       コールバックは発行元のタスクの上で走るので、そこで判定すると申告した Read / Write が効かず、
//       HitboxComponent を書く CombatAnimationSyncSystem と同時に走ってしまう
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Combat/Components/CombatComponents.h"
#include "Game/Logics/Combat/Components/HealthComponent.h"

#include <DirectXMath.h>
#include <vector>

class HitboxCollisionSystem : public CCL::ECS::IfSystem<
    HitboxCollisionSystem,
    CCL::ECS::Write<HitboxComponent>,   // 多段ヒットの履歴（hitTargets / hitCount）を書く
    CCL::ECS::Read<HurtboxComponent>,   // 受けるダメージの倍率を読む
    CCL::ECS::Read<HealthComponent>,    // 無敵中の相手を履歴に残さないため読む
    CCL::ECS::Read<TransformComponent>> // ノックバック方向の算出に読む
{
public:
    HitboxCollisionSystem() : IfSystem("HitboxCollisionSystem") {}

    void Initialize() override;
    void Update(float dt) override;

    // 判定に使う入力がそろってから走る。同じ L05_Collision なので、書かないと順番が決まらない
    //   JoltCollisionEventSystem  … 前に走ると、そのフレームの接触を取りこぼす
    //   CombatAnimationSyncSystem … 前に走ると、isActive が1フレーム古くなる
    //   JoltPullSystem            … ノックバックの向きを、物理の結果を書き戻した後の Transform で決める
    std::vector<const char *> GetRunAfter() const override
    {
        return {"JoltCollisionEventSystem", "CombatAnimationSyncSystem", "JoltPullSystem"};
    }

private:
    // attackerCandidate が victimCandidate に当てたかを調べ、当たっていれば DamageEvent を作る
    // どちらが攻撃側か分からないので、呼ぶ側が A→B と B→A の2回呼ぶ。contactPos は火花を出す位置
    void CheckAndSpawnDamageEvent(CCL::ECS::EntityID       attackerCandidate,
                                  CCL::ECS::EntityID       victimCandidate,
                                  const DirectX::XMFLOAT3 &contactPos);

    // このフレームに届いた接触。コールバックが足し、Update が処理して空にする
    // 発行元の JoltCollisionEventSystem は GetRunAfter で先に走るので、足すのと処理が同時に走らない
    std::vector<JoltCollisionEvent> m_pendingContacts;

    // 再ヒット間隔を測る時計（秒）。hitTimes はこの値で記録する
    // Hitbox ごとに経過時間を数えると全 Hitbox を毎フレーム回すので、1本の時計の差で測る
    float m_clock = 0.0f;
};