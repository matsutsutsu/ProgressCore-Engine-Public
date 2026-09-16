// HitboxComponent を主役にして走査し、ownerEntity の Animator を読んで
// Hitbox のアクティブ状態を同期する「イテレーション反転」設計。
//
// Write<HitboxComponent> を正しく宣言。
// Animator は別エンティティへの「読み取り専用」GetComponent で参照。
// 読み取りのみなので、Animator を Write するシステムと同バッチにならない限りレースしない。
// 本システムは L05_Collision(500)、Animator 書き込み系は L02_Update(200)/L02_PostUpdate(250)
// なので、ステージの壁により完全に直列化されており安全が保証される。
#pragma once
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
