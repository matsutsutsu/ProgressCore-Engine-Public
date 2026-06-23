/**
 * @file CombatAnimationSyncSystem.h
 * @brief HitboxComponent を主役にして走査し、ownerEntity の Animator を読んで
 *        Hitbox のアクティブ状態を同期する「イテレーション反転」設計。
 *
 * 【旧実装との違い】
 *   旧: Read<Animator>, Read<Roster> を宣言しつつ、別エンティティの HitboxComponent を
 *       GetComponent 経由で書き換え → スケジューラが書き込みを認識できずレースの温床。
 *
 *   新: Write<HitboxComponent> を正しく宣言。
 *       Animator は別エンティティへの「読み取り専用」GetComponent で参照。
 *       読み取りのみなので、Animator を Write するシステムと同バッチにならない限りレースしない。
 *       本システムは L05_Collision(500)、Animator 書き込み系は L02_Update(200)/L02_PostUpdate(250)
 *       なので、ステージの壁により完全に直列化されており安全が保証される。
 */
#pragma once
#include "Engine/ECS/System/CCL_System.h"
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