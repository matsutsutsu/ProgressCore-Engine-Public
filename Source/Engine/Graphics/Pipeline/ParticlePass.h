#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

/// @brief パーティクルシステムを描画するパス（半透明パスの後）。
class ParticlePass : public IRenderPass
{
public:
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "ParticlePass"; }
};