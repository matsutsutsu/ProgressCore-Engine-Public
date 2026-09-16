#include "SkyboxPass.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/RenderState.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

void SkyboxPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);

    // Initialize(const RenderServices&) に変更したため device を services から取得する
    ID3D11Device *device = services.device;

    HRESULT hr = GpuResourceUtils::LoadVertexShader(
        device,
        "Assets/Shader/SkyboxVS.cso",
        nullptr, 0,
        nullptr,
        &m_vertexShader);
    _ASSERT_EXPR(SUCCEEDED(hr), L"SkyboxVS のロードに失敗");

    hr = GpuResourceUtils::LoadPixelShader(
        device,
        "Assets/Shader/SkyboxPS.cso",
        &m_pixelShader);
    _ASSERT_EXPR(SUCCEEDED(hr), L"SkyboxPS のロードに失敗");

    hr = GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(CbSkybox), &m_cbSkybox);
    _ASSERT_EXPR(SUCCEEDED(hr), L"Skybox 用定数バッファの作成に失敗");

    hr = GpuResourceUtils::LoadTexture(
        device,
        "Assets/Textures/IBL/skybox_highres.dds",
        &m_skyboxTex);
    _ASSERT_EXPR(SUCCEEDED(hr), L"Skybox 背景テクスチャのロードに失敗");
}

void SkyboxPass::Execute(const RenderFrameContext &ctx)
{

    if (!ctx.camera) return;
    ID3D11DeviceContext *dc = ctx.dc;

    // 逆 View-Projection 行列を計算
    const DirectX::XMMATRIX V     = DirectX::XMLoadFloat4x4(&ctx.camera->GetView());
    const DirectX::XMMATRIX P     = DirectX::XMLoadFloat4x4(&ctx.camera->GetProjection());
    const DirectX::XMMATRIX invVP = DirectX::XMMatrixInverse(nullptr, V * P);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_cbSkybox.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        CbSkybox *cb = static_cast<CbSkybox *>(mapped.pData);
        DirectX::XMStoreFloat4x4(&cb->invViewProj, invVP);
        dc->Unmap(m_cbSkybox.Get(), 0);
    }

    dc->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    dc->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    dc->VSSetConstantBuffers(SLOT_CB_SKYBOX, 1, m_cbSkybox.GetAddressOf());

    // 背景テクスチャは EnvironmentSystem が SkyboxComponent から解決したものを ctx 経由で受け取る。
    // 無ければ（コンポーネント/EnvironmentSystem 不在時）この Pass 自前の既定へフォールバックする。
    ID3D11ShaderResourceView *skyTex = ctx.skyboxSRV ? ctx.skyboxSRV : m_skyboxTex.Get();
    dc->PSSetShaderResources(SLOT_SRV_SKYBOX_BG, 1, &skyTex);

    dc->IASetInputLayout(nullptr);
    dc->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    dc->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const RenderState *rs = m_services->renderState;

    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
    // 深度は書かず LESS_EQUAL で通す（Z=1.0 の奥底に描くため 1.0 <= 1.0 を許容する）
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestOnly), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullNone));

    dc->Draw(3, 0);

    // ステート復元
    dc->OMSetBlendState(rs->GetBlendState(BlendState::Opaque), nullptr, 0xFFFFFFFF);
    dc->OMSetDepthStencilState(rs->GetDepthStencilState(DepthState::TestAndWrite), 0);
    dc->RSSetState(rs->GetRasterizerState(RasterizerState::SolidCullBack));
}