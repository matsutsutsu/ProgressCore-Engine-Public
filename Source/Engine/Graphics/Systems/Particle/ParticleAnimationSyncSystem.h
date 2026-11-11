#pragma once
// ===========================================================================
// 概要: アニメーションの Particle 区間イベントに合わせて、GPUParticleComponent の
//       emitting を切り替える System（L05_Collision）
//
// 責務:
//   - animControlled なエミッタについて、Animator を持つ祖先を探して ownerEntity に持つ
//   - 祖先の今の再生時刻が Particle 区間に入っているかで emitting を決める
//
// 備考: 剣のボーンの子に付けたエミッタを、攻撃アニメの指定区間だけ出すのに使う
//       親をたどって最初に見つかった Animator の持ち主を owner にするので、インスペクタで指定しなくてよい
//       AnimationSystem の後、GPUParticleUpdateSystem の前に走るので、同じフレームでそろう
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"

class ParticleAnimationSyncSystem : public CCL::ECS::IfSystem<
    ParticleAnimationSyncSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<GPUParticleComponent>>
{
public:
    ParticleAnimationSyncSystem() : IfSystem("ParticleAnimationSyncSystem") {}

    void Update(float dt) override;
};
