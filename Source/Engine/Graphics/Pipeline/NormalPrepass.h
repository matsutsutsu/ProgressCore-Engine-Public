/// @file  NormalPrepass.h
/// @brief 不透明ジオメトリのビュー空間法線を法線バッファへ書き出すパス。
///        役割      : SSAO/SSR等のスクリーンスペース技術に「法線」という情報源を供給する。
///        責務      : 不透明コマンドを専用シェーダーで再提出し、NormalBuffer へ描く。
///        位置付け  : OpaquePass の直後に実行する(方式B: 専用プリパス)。
#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"
#include "Engine/Graphics/Pipeline/NormalBuffer.h"
#include <vector>

/// @brief 不透明ジオメトリの法線を書き出すプリパス。
class NormalPrepass : public IRenderPass
{
public:
    void Initialize(const RenderServices& services) override;
    void Execute(const RenderFrameContext& ctx) override;
    void OnResize(uint32_t width, uint32_t height) override;
    const char* GetName() const override { return "NormalPrepass"; }

    ID3D11ShaderResourceView* GetNormalSRV() const noexcept { return _normalBuffer.GetSRV(); }

private:
    PipelineState  _pso;
    InstanceBuffer _instanceBuffer;
    NormalBuffer   _normalBuffer;
    std::vector<InstanceData> _instanceScratch;
};
