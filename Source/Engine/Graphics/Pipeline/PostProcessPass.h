#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"

// HDR→SDR 変換・ブルーム合成を行い、最終出力先（targetRTV）に書き出すパス。
class PostProcessPass : public IRenderPass
{
public:
    void        Execute(const RenderFrameContext &ctx) override;
    const char *GetName() const override { return "PostProcessPass"; }
};