#pragma once
// ===========================================================================
// 概要: アニメーションの Particle 区間イベントに合わせて、GPUParticleComponent の
//       emitting を切り替える System（L05_Collision）。
//
// 責務:
//   - animControlled なエミッタについて、Animator を持つ祖先を探して ownerEntity に持つ
//   - 祖先の今の再生時刻が Particle 区間に入っているかで emitting を決める
//
// 備考: 剣のボーンに子として付けたエミッタを、攻撃アニメの指定区間だけ出すために使う。
//       走査するのは Animator 側ではなくエミッタ側（TrailAnimationSyncSystem と同じ形）。
//       書き込むのは自分の GPUParticleComponent だけで、祖先の Animator は GetComponent で読むだけ。
//       エミッタは BoneAttachment などでキャラの子孫に付いている前提なので、親をたどって最初に
//       見つかった Animator の持ち主を owner にする（インスペクタで owner を指定しなくてよい）。
//       AnimationSystem（L02_PostUpdate）が再生時刻を進めた後、描画側の
//       GPUParticleUpdateSystem（R07）が emitting を読む前に走るので、同じフレームで揃う。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"

class ParticleAnimationSyncSystem
    : public CCL::ECS::IfSystem<ParticleAnimationSyncSystem,
                                CCL::ECS::Read<TransformComponent>,
                                CCL::ECS::Write<GPUParticleComponent>>
{
public:
    ParticleAnimationSyncSystem() : IfSystem("ParticleAnimationSyncSystem") {}

    void Update(float dt) override;
};
