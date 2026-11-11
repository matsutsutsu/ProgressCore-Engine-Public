#pragma once
// ===========================================================================
// 概要: ParticleRenderer でパーティクルを描くパス
//
// 責務:
//   - RenderServices の particleRenderer に描画を任せる
//
// 備考: 半透明（TransparentPass）の後に置く
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class ParticlePass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
