#pragma once
// ===========================================================================
// 概要: 不透明のメッシュを先に1回描き、深度とビュー空間の法線を書き出すパス。
//
// 責務:
//   - 不透明のコマンドを法線用のシェーダーで描き、NormalBuffer（SSAO が t6 で読む）に書く
//   - 深度も書いて、後の OpaquePass で early-Z が効くようにする
//   - メッシュごとに視錐台カリングし、見えるインスタンスの列を RenderQueue に残す
//     （OpaquePass は同じ列を使い、カリングをやり直さない）
//   - LOD のあるメッシュは、インスタンスごとに段を選んで段の順に並べ、段ごとに描く
//
// 備考: OpaquePass より前に置く。LOD の段はここで決め、OpaquePass はそれに従う。
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include "Engine/Graphics/RHI/PipelineState.h"
#include "Engine/Graphics/Renderer/Core/InstanceBuffer.h"
#include "Engine/Graphics/RHI/NormalBuffer.h"
#include <vector>

class NormalPrepass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;
    void OnResize(uint32_t width, uint32_t height) override;

    ID3D11ShaderResourceView *GetNormalSRV() const noexcept { return m_normalBuffer.GetSRV(); }

private:
    /**
     * @brief 法線バッファの SRV を Graphics に渡す（Graphics::MakeFrameContext が ctx.sceneNormalSRV に入れる）。
     * @note SRV を作った・作り直した直後に呼ぶ。
     */
    void PublishNormalSRV();

    PipelineState             m_pso;
    InstanceBuffer            m_instanceBuffer;
    NormalBuffer              m_normalBuffer;
    std::vector<InstanceData> m_instanceScratch; // メッシュごとに使い回す作業用の配列
    std::vector<InstanceData> m_lodSortTemp;     // LOD の段の順に並べ替えるときの作業用の配列
};
