#pragma once
// ===========================================================================
// 概要: 武器のトレイル（背景を歪ませる層と、加算合成の帯）を描くパス。
//
// 責務:
//   - TrailRenderer にこのフレームの頂点を描かせる
//
// 備考: 歪みは SceneColorGrabPass が写したシーンの色を読むので、それより後に置く。
//       ブルームが明るい軌跡を拾えるよう、ポストプロセスより前に置く。
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

class TrailPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext &ctx) override;
};
