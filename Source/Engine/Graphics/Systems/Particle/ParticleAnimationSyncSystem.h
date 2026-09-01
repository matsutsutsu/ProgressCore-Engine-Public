#pragma once
// ===========================================================================
// ParticleAnimationSyncSystem.h
// -----------------------------
// 役割:
//   animControlled な GPUParticleComponent を主役に走査し、その「Animator を持つ
//   祖先」の Particle 区間イベントを読んで emitting を毎フレーム同期する。
//   剣ボーンに子付けしたエミッタを、攻撃アニメの指定区間だけ「まとわせる」ための駆動。
//
// 設計方針（TrailAnimationSyncSystem と全く同じイテレーション反転）:
//   - Write<GPUParticleComponent> を宣言し、自エンティティの発生ゲートのみ書き込む。
//     祖先の Animator は別エンティティへの読み取り専用参照（GetComponent）。
//   - エミッタは BoneAttachment 等でキャラの子孫にぶら下がる前提。親を辿って最初に
//     見つかった Animator 保持エンティティを owner として一度だけキャッシュする
//     （＝インスペクタでの手動オーナー指定が不要）。
//   - 本システムは L05_Collision。AnimationSystem(L02_PostUpdate) が currentTimer を
//     進めた後・描画の GPUParticleUpdateSystem(R07) が emitting を読む前に走るため、
//     同フレームで整合する。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"

#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"

// Particle アニメイベント区間で GPUParticleComponent::emitting を駆動するシステム。
class ParticleAnimationSyncSystem
    : public CCL::ECS::IfSystem<ParticleAnimationSyncSystem,
          CCL::ECS::Read<TransformComponent>,
          CCL::ECS::Write<GPUParticleComponent>>
{
public:
    ParticleAnimationSyncSystem() : IfSystem("ParticleAnimationSyncSystem") {}

    void Update(float dt) override;
};
