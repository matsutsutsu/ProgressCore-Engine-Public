#include "TrailRenderer.h"

#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

#include <algorithm>
#include <cstring>

using namespace DirectX;

namespace
{
    // b0 の定数バッファ。ShaderShared.h と同じく HLSL 側も row_major で受けるので、
    // Camera の行列を転置せずにそのまま送る
    struct CbTrail
    {
        XMFLOAT4X4 viewProjection;
    };
}

// ---------------------------------------------------------------------------
// 初期化
// ---------------------------------------------------------------------------

void TrailRenderer::Initialize(ID3D11Device *device)
{
    if (m_initialized || !device)
    {
        return;
    }

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    GpuResourceUtils::LoadVertexShader(
        device, "Assets/Shader/TrailVS.cso",
        layout, _countof(layout),
        m_inputLayout.GetAddressOf(), m_vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(
        device, "Assets/Shader/TrailPS.cso", m_pixelShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(
        device, "Assets/Shader/TrailDistortPS.cso", m_distortPixelShader.GetAddressOf());

    // 頂点バッファ（毎フレーム WRITE_DISCARD で書き直す。加算・歪みの順に詰める）
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth         = static_cast<UINT>(sizeof(Vertex) * MAX_VERTICES);
    vbDesc.Usage             = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags         = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags    = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&vbDesc, nullptr, m_vertexBuffer.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(
        device, sizeof(CbTrail), m_constantBuffer.GetAddressOf());

    // テクスチャのないトレイル用の 1x1 の白（頂点の色 × 白 で、頂点の色がそのまま出る）
    GpuResourceUtils::CreateDummyTexture(device, 0xFFFFFFFF, m_whiteTex.GetAddressOf());

    // 加算の層。Additive は (SrcAlpha, One) なので、頂点のアルファがそのまま足す強さになる
    // （rgb に前もってアルファを掛けておく必要はない）
    PipelineStateDesc desc;
    desc.blend      = BlendState::Additive;
    desc.depth      = DepthState::TestOnly;
    desc.rasterizer = RasterizerState::SolidCullNone;
    desc.topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    m_pso           = PipelineState(desc);

    // 歪みの層。ずらした背景で置き換えるのでアルファブレンド。RGB だけを書く
    // UITransparency を使い、HDR バッファのアルファ（1）を壊さない
    desc.blend   = BlendState::UITransparency;
    m_distortPso = PipelineState(desc);

    m_initialized = true;
}



// ---------------------------------------------------------------------------
// 三角形の受け付け
// ---------------------------------------------------------------------------

size_t TrailRenderer::ClampToCapacity(size_t vertexCount) const
{
    const size_t used = m_vertices.size() + m_distortVertices.size();
    if (used + vertexCount <= MAX_VERTICES)
        return vertexCount;

    // 入りきらないぶんは、三角形の区切り（3頂点ずつ）で切り捨てる
    size_t remain = MAX_VERTICES - used;
    remain -= remain % 3;
    return std::min(vertexCount, remain);
}



void TrailRenderer::SubmitTriangleList(const Vertex *verts, size_t vertexCount,
                                       ID3D11ShaderResourceView *texture)
{
    if (!verts || vertexCount == 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    vertexCount = ClampToCapacity(vertexCount);
    if (vertexCount == 0)
        return;

    m_vertices.insert(m_vertices.end(), verts, verts + vertexCount);

    // 直前の区間と同じテクスチャならつなげて、1回のドローコールで描く
    if (!m_addBatches.empty() && m_addBatches.back().srv == texture)
        m_addBatches.back().count += vertexCount;
    else
        m_addBatches.push_back({texture, vertexCount});
}



void TrailRenderer::SubmitDistortTriangleList(const Vertex *verts, size_t vertexCount)
{
    if (!verts || vertexCount == 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    vertexCount = ClampToCapacity(vertexCount);
    if (vertexCount == 0)
        return;

    m_distortVertices.insert(m_distortVertices.end(), verts, verts + vertexCount);
}



// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------

void TrailRenderer::Render(const RenderServices &services, const RenderFrameContext &ctx)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_vertices.empty() && m_distortVertices.empty())
        return;

    // 描けないときも、次のフレームに持ち越さないよう捨てる
    if (!m_initialized || !ctx.dc || !ctx.camera || !services.renderState)
    {
        ClearPending();
        return;
    }

    ID3D11DeviceContext *dc = ctx.dc;

    const size_t additiveCount = m_vertices.size();
    const size_t distortCount  = m_distortVertices.size();

    {
        CbTrail cb;
        cb.viewProjection = ctx.camera->GetViewProjectionMatrix();
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            dc->Unmap(m_constantBuffer.Get(), 0);
        }
    }

    // 加算の頂点を先頭に、歪みの頂点をその後ろに詰める
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            Vertex *dst = static_cast<Vertex *>(mapped.pData);
            if (additiveCount > 0)
                std::memcpy(dst, m_vertices.data(), additiveCount * sizeof(Vertex));
            if (distortCount > 0)
                std::memcpy(dst + additiveCount, m_distortVertices.data(), distortCount * sizeof(Vertex));
            dc->Unmap(m_vertexBuffer.Get(), 0);
        }
    }

    // 描画先は明示的にバインドしない（前のパスがバインドしたままの HDR バッファに重ねて描く）
    dc->IASetInputLayout(m_inputLayout.Get());

    UINT          stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer *vb = m_vertexBuffer.Get();
    dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);

    ID3D11Buffer *cb = m_constantBuffer.Get();
    dc->VSSetConstantBuffers(0, 1, &cb); // b0

    // 歪みの層。加算の芯を上に重ねるので先に描く
    // シーンの色（t5）は前のパスが別の SRV で上書きしている場合があるので、ここでバインドし直す
    // 読むのは写したテクスチャなので、描いている HDR バッファを同時に読むことにはならない
    ID3D11ShaderResourceView *sceneColorSRV = nullptr;
    if (distortCount > 0 && services.postProcess)
        sceneColorSRV = services.postProcess->GetSceneColorSRV();

    if (sceneColorSRV)
    {
        // 歪みのピクセルシェーダーは gView / gTimeParams / gScreenParams を CbScene（b8）から読む
        if (services.sceneConstantBuffer)
        {
            ID3D11Buffer *sceneCb = services.sceneConstantBuffer;
            dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneCb);
        }

        ID3D11SamplerState *sampler = services.renderState->GetSamplerState(SamplerState::LinearClamp);
        dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &sampler);
        dc->PSSetShaderResources(SLOT_SRV_SCENE_COLOR, 1, &sceneColorSRV);

        m_distortPso.Apply(dc, services.renderState);
        dc->PSSetShader(m_distortPixelShader.Get(), nullptr, 0);
        dc->Draw(static_cast<UINT>(distortCount), static_cast<UINT>(additiveCount));

        // 後のパスに SRV のバインドを残さない
        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_SCENE_COLOR, 1, &nullSRV);
    }

    // 加算の層。テクスチャが変わる区間ごとに描く
    if (additiveCount > 0)
    {
        m_pso.Apply(dc, services.renderState);
        dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        // トレイルのテクスチャは繰り返したりスクロールさせたりするので Wrap
        ID3D11SamplerState *trailSmp = services.renderState->GetSamplerState(SamplerState::LinearWrap);
        dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &trailSmp);

        UINT start = 0;
        for (const AddBatch &batch : m_addBatches)
        {
            if (batch.count == 0)
                continue;
            ID3D11ShaderResourceView *srv = batch.srv ? batch.srv : m_whiteTex.Get();
            dc->PSSetShaderResources(0, 1, &srv); // t0: gTrailTex
            dc->Draw(static_cast<UINT>(batch.count), start);
            start += static_cast<UINT>(batch.count);
        }

        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->PSSetShaderResources(0, 1, &nullSRV);
    }

    // 後のパスにステートを残さない
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->RSSetState(nullptr);
    dc->OMSetDepthStencilState(nullptr, 0);
    dc->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    ClearPending();
}



void TrailRenderer::ClearPending()
{
    m_vertices.clear();
    m_addBatches.clear();
    m_distortVertices.clear();
}
