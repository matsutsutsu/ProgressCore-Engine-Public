#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

// 武器トレイル（歪みレイヤ＋加算リボン）を描画するパス。半透明/パーティクルの後、
// ポストプロセスの前に置き、Bloom が明るい軌跡を拾えるようにする。
//
// 歪みは SceneColorGrabPass が確保したシーンカラーを読むため、
// このパスは必ずグラブより後になければならない。
class TrailPass : public IRenderPass
{
public:
    void        Execute(const RenderFrameContext &ctx) override;
    const char *GetName() const override { return "TrailPass"; }
};
