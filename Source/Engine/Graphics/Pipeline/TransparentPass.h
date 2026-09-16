#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"

/**
 * @brief 半透明メッシュを奥から手前の順で描画するパス。
 * @details
 *  OpaquePass が blend 有効と判定して転送した遅延コマンドを、距離降順で
 *  ソートしてから 1 体ずつ描画する。
 */
class TransparentPass : public IRenderPass
{
public:
    void        Initialize(const RenderServices &services) override;
    void        Execute(const RenderFrameContext &ctx) override;
    const char *GetName() const override { return "TransparentPass"; }

private:
    PipelineState  m_psoTransparent;
    InstanceBuffer m_instanceBuffer;
};