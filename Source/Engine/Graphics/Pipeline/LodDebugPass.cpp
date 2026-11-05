#include "LodDebugPass.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <algorithm>
#include <cstring>

namespace
{
    // b0 の定数バッファ（LodDebugPS.hlsl の CbLodDebug と同じ並び）
    struct CbLodDebug
    {
        DirectX::XMFLOAT4 color; // rgb = 段の色、a = 重ねる濃さ
    };

    // 段ごとの色。a は重ねる濃さ
    // 半分にして、下の陰影が透けて形も見えるようにしている
    constexpr DirectX::XMFLOAT4 kLevelColors[MeshLod::kMaxLevels] = {
        {0.1f, 1.0f, 0.1f, 0.5f},  // LOD0：緑
        {1.0f, 0.9f, 0.1f, 0.5f},  // LOD1：黄
        {1.0f, 0.15f, 0.1f, 0.5f}, // LOD2：赤
        {0.2f, 0.4f, 1.0f, 0.5f},  // LOD3：青（今は使っていない）
    };
} // namespace



void LodDebugPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);

    // 既定はオフ。エディタのレンダーデバッグのウィンドウから切り替える
    enabled = false;

    // 色は半透明で重ねる。UITransparency は RT のアルファを書き換えないので、
    // エディタの GameView（アルファ付きで貼る）に穴が開かない
    PipelineStateDesc desc = PipelineStateDesc::DefaultOpaque();
    desc.blend             = BlendState::UITransparency;

    // 深度は LESS_EQUAL のテストだけ
    // NormalPrepass と同じ形を描くので同じ深度になり、表に見えている面にだけ色が乗る
    desc.depth = DepthState::TestOnly;
    m_pso      = PipelineState(desc);

    m_instanceBuffer.Create(services.device, 8192);

    GpuResourceUtils::LoadPixelShader(services.device, "Assets/Shader/LodDebugPS.cso", m_pixelShader.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(services.device, sizeof(CbLodDebug), m_colorBuffer.GetAddressOf());
}



DirectX::XMFLOAT4 LodDebugPass::GetLevelColor(size_t level)
{
    return kLevelColors[(std::min)(level, MeshLod::kMaxLevels - 1)];
}



void LodDebugPass::Execute(const RenderFrameContext &ctx)
{
    // 処理フロー:
    // 1. 描く準備（NormalPrepass の頂点シェーダーに、このパスのピクセルシェーダーを組み合わせる）
    // 2. NormalPrepass と同じ順でバッチとメッシュを回し、残された見えるインスタンスの列を読む
    // 3. 段ごとに色を入れ替えて描く

    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc || !m_pixelShader || !m_colorBuffer) return;

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // ---------------------------------------------------------
    // 1. 描く準備
    // ---------------------------------------------------------

    // 頂点シェーダー・入力レイアウト・定数バッファは NormalPrepass のものを使い、
    // ピクセルシェーダーだけをこのパスのものに差し替える
    Shader *shader = m_services->shaders->GetShader("NormalPrepass"_hash, m_services->device);
    if (!shader) return;

    m_pso.Apply(dc, m_services->renderState);
    modelConstants->BindAllGlobalConstantBuffers(dc);

    shader->Begin(dc, *m_services, ctx);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->PSSetConstantBuffers(SLOT_CB_MATERIAL, 1, m_colorBuffer.GetAddressOf());
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_lastLevelCounts = {};

    // ---------------------------------------------------------
    // 2. バッチとメッシュを回す
    // ---------------------------------------------------------

    // バッチの区切り方は NormalPrepass と同じにする
    // 見えるインスタンスの列のキーが (バッチの先頭, メッシュ) のため
    const auto &sortKeys       = queue->GetSortedKeys();
    const auto &opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size())
    {
        const size_t   batchStart = i;
        const uint64_t batchKey   = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == batchKey)
        {
            ++i;
        }

        const RenderCommand &first = opaqueCommands[sortKeys[batchStart].originalIndex];
        if (!first.model) continue;

        const auto &meshes = first.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
        {
            const Model::Mesh &mesh = meshes[meshIdx];

            // LOD のないメッシュには色を付けない
            if (!mesh.data || mesh.data->lods.empty()) continue;

            // NormalPrepass が描かなかったメッシュ（半透明のバッチなど）は列がないので飛ばす
            const InstanceData  *visibleData  = nullptr;
            size_t               visibleCount = 0;
            MeshLod::LevelCounts levelCounts{};
            if (!queue->FindVisibleInstances(RenderQueue::MakeVisibleKey(batchStart, meshIdx),
                                             visibleData, visibleCount, levelCounts))
            {
                continue;
            }
            if (visibleCount == 0) continue;

            UINT stride = sizeof(ModelResource::Vertex), offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);

            // ---------------------------------------------------------
            // 3. 段ごとに色を入れ替えて描く
            // ---------------------------------------------------------

            size_t levelStart = 0; // この段の先頭が visibleData の何番目か
            for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
            {
                const size_t levelCount = levelCounts[level];
                if (levelCount == 0) continue;

                UpdateColor(dc, level);

                const MeshLod::DrawRange range = MeshLod::GetDrawRange(*mesh.data, level);
                dc->IASetIndexBuffer(range.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                size_t curOffset = 0;
                while (curOffset < levelCount)
                {
                    const size_t drawCount = (std::min)(levelCount - curOffset, m_instanceBuffer.GetMaxInstances());
                    m_instanceBuffer.Write(dc, visibleData + levelStart + curOffset, drawCount);
                    m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    dc->DrawIndexedInstanced(range.indexCount, static_cast<UINT>(drawCount), range.startIndex, 0, 0);
                    curOffset += drawCount;
                }

                levelStart += levelCount;
                m_lastLevelCounts[level] += static_cast<uint32_t>(levelCount);
            }
        }
    }

    m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    shader->End(dc, *m_services);
}



void LodDebugPass::UpdateColor(ID3D11DeviceContext *dc, size_t level)
{
    CbLodDebug cb;
    cb.color = GetLevelColor(level);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_colorBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        dc->Unmap(m_colorBuffer.Get(), 0);
    }
}
