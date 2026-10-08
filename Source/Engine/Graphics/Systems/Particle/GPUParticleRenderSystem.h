#pragma once
// ===========================================================================
// 概要: GPU パーティクルの描画コマンドを ParticleRenderer に積む System（R08_Main）。
//
// 責務:
//   - エミッタごとに、バッファが用意できていて表示中なら描画コマンドを1つ積む
//   - 破棄時に全エミッタの粒子バッファを解放する
//
// 備考: 描くかどうかは「GPU バッファが用意できているか」と previewVisible だけで決める。
//       親を持たない（parent==0 の）Entity を除外してはいけない。VFXSystem がワールドに置く
//       VFX はルートにエミッタを持つので、除外するとエラーも出ずに描かれなくなる。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class GPUParticleRenderSystem : public CCL::ECS::IfSystem<GPUParticleRenderSystem,
                                                          CCL::ECS::Write<GPUParticleComponent>,
                                                          CCL::ECS::Read<TransformComponent>>
{
public:
    GPUParticleRenderSystem() : IfSystem("GPUParticleRenderSystem") {}
    ~GPUParticleRenderSystem() { ReleaseAllResources(); }

    void Update(float dt) override;

    // 全エミッタの粒子バッファを解放し、次に使うときに作り直させる
    void ReleaseAllResources();
};
