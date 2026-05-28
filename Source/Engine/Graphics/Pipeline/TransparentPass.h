#pragma once
#include "IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

/**
 * @brief 半透明メッシュを奥から手前の順で描画するパス。
 * @details
 *  OpaquePass から転送された遅延コマンドと、ECS から直接 Submit されたコマンドを
 *  合算し、距離降順でソートしてから描画する。
 */
class TransparentPass : public IRenderPass
{
public:
    void Initialize(const RenderServices& services) override;
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "TransparentPass"; }

private:
    PipelineState  _psoTransparent;
    InstanceBuffer _instanceBuffer;
};