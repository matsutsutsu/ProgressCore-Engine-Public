#pragma once
// ===========================================================================
// 概要: 攻撃判定（HitboxComponent）を、持ち主のアニメーションの HitBox 区間に合わせて開け閉めする System。
//
// 責務:
//   - Hitbox の親をたどって Animator を持つ持ち主を探し、覚えておく
//   - 今のフレームで進んだ再生時間が HitBox 区間に重なるかを調べ、isActive を切り替える
//   - 区間に入ったフレームで多段ヒットの履歴を消し、区間の攻撃力・ノックバック・ヒットストップを書く
//
// 備考: Hitbox の側から回し、持ち主の Animator は GetComponent で読むだけにする。
//       Animator を書く System は L02_Update / L02_PostUpdate、この System は L05_Collision なので、
//       ステージの間で必ず順番に走り、同時に触ることはない。
//       親を持たない判定（SpawnHitboxSystem が作る球・ドローン）は持ち主が見つからないので触らない。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Combat/CombatComponents.h"

class CombatAnimationSyncSystem
    : public CCL::ECS::IfSystem<CombatAnimationSyncSystem,
                                CCL::ECS::Write<HitboxComponent>>
{
public:
    CombatAnimationSyncSystem()
        : IfSystem("CombatAnimationSyncSystem")
    {
    }
    void Update(float dt) override;
};
