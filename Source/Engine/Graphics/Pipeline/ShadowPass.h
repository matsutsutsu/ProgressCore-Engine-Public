#pragma once
#include "IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

/**
 * @brief ライト視点でシャドウマップ（カスケード 3 枚）を生成するパス。
 */
class ShadowPass : public IRenderPass
{
public:
    void Initialize(const RenderServices& services) override;
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "ShadowPass"; }

private:
    PipelineState  _psoShadow;
    InstanceBuffer _instanceBuffer;
};