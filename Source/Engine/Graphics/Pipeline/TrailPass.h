#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

/// @brief 武器トレイル（加算リボン）を描画するパス。半透明/パーティクルの後、
///        ポストプロセスの前に置き、Bloom が明るい軌跡を拾えるようにする。
class TrailPass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "TrailPass"; }
};
