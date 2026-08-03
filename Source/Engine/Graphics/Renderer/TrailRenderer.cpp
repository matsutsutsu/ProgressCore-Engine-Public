// ===========================================================================
// TrailRenderer.cpp
// 役割・設計方針は TrailRenderer.h を参照。
//
// 実装の意図(Why):
//   - viewProjection は Camera からそのまま(転置なし)アップロードする。エンジン共通の
//     ShaderShared.h が row_major を採用しており、HLSL 側も row_major で受けるため一致する。
//   - Additive ブレンドは (SrcAlpha, One)。従って頂点アルファがそのまま加算強度になり、
//     rgb を事前乗算(premultiply)する必要はない。
//   - RTV/DSV は明示バインドしない。先行パスが張った HDR ターゲットに重ねて描く。
// ===========================================================================
#include "TrailRenderer.h"

#include "Engine/Graphics/RHI/RenderContext.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include <algorithm>
#include <cstring>

using namespace DirectX;

namespace
{
// b0 に積む定数バッファ。row_major で HLSL の cbuffer と一致させる。
struct CbTrail
{
    XMFLOAT4X4 viewProjection;
};
} // namespace

void TrailRenderer::EnsureInitialized(ID3D11Device* device)
{
    if (m_initialized || !device)
        return;

    // 入力レイアウト: POSITION(float3) + COLOR(float4)
    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    GpuResourceUtils::LoadVertexShader(
        device, "Assets/Shader/TrailVS.cso",
        layout, _countof(layout),
        m_inputLayout.GetAddressOf(), m_vertexShader.GetAddressOf());

    GpuResourceUtils::LoadPixelShader(
        device, "Assets/Shader/TrailPS.cso", m_pixelShader.GetAddressOf());

    // 動的頂点バッファ（毎フレーム WRITE_DISCARD で上書き）
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = static_cast<UINT>(sizeof(Vertex) * MAX_VERTICES);
    vbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    vbDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&vbDesc, nullptr, m_vertexBuffer.GetAddressOf());

    // 動的定数バッファ（viewProjection）
    GpuResourceUtils::CreateDynamicConstantBuffer(
        device, sizeof(CbTrail), m_constantBuffer.GetAddressOf());

    // PSO: 加算 / 深度テストのみ / 両面 / 三角形リスト
    PipelineStateDesc desc;
    desc.blend      = BlendState::Additive;
    desc.depth      = DepthState::TestOnly;
    desc.rasterizer = RasterizerState::SolidCullNone;
    desc.topology   = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    m_pso = PipelineState(desc);

    m_initialized = true;
}

void TrailRenderer::SubmitTriangleList(const Vertex* verts, size_t vertexCount)
{
    if (!verts || vertexCount == 0)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // 容量超過分は三角形境界(3頂点単位)で切り捨てる。
    if (m_vertices.size() + vertexCount > MAX_VERTICES)
    {
        size_t remain = MAX_VERTICES - m_vertices.size();
        remain -= remain % 3;
        if (remain == 0)
            return;
        vertexCount = std::min(vertexCount, remain);
    }

    m_vertices.insert(m_vertices.end(), verts, verts + vertexCount);
}

void TrailRenderer::Render(const RenderServices& services, const RenderFrameContext& ctx)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_vertices.empty())
        return;

    // 前提が欠けていたら安全側で破棄（次フレームに持ち越さない）。
    if (!ctx.dc || !ctx.camera || !services.device || !services.renderState)
    {
        m_vertices.clear();
        return;
    }

    EnsureInitialized(services.device);
    if (!m_initialized)
    {
        m_vertices.clear();
        return;
    }

    ID3D11DeviceContext* dc = ctx.dc;

    // 1. 定数バッファ（viewProjection をそのまま）
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

    // 2. 頂点アップロード
    const size_t vertexCount = std::min(m_vertices.size(), MAX_VERTICES);
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            std::memcpy(mapped.pData, m_vertices.data(), vertexCount * sizeof(Vertex));
            dc->Unmap(m_vertexBuffer.Get(), 0);
        }
    }

    // 3. ステート＆描画
    m_pso.Apply(dc, services.renderState);
    dc->IASetInputLayout(m_inputLayout.Get());

    UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vb = m_vertexBuffer.Get();
    dc->IASetVertexBuffers(0, 1, &vb, &stride, &offset);

    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    ID3D11Buffer* cb = m_constantBuffer.Get();
    dc->VSSetConstantBuffers(0, 1, &cb); // b0

    dc->Draw(static_cast<UINT>(vertexCount), 0);

    // 4. 後続パスへ状態を残さない（PrimitiveRenderer に倣う）
    dc->VSSetShader(nullptr, nullptr, 0);
    dc->PSSetShader(nullptr, nullptr, 0);
    dc->RSSetState(nullptr);
    dc->OMSetDepthStencilState(nullptr, 0);
    dc->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    m_vertices.clear();
}

void TrailRenderer::Shutdown()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_vertices.clear();
    m_vertexBuffer.Reset();
    m_constantBuffer.Reset();
    m_inputLayout.Reset();
    m_vertexShader.Reset();
    m_pixelShader.Reset();
    m_initialized = false;
}
