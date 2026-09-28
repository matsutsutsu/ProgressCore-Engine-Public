// 4回に分ける理由: 半球の中をいくつかサンプリングして求めた値は粒の粗いノイズが多く、
// そのまま色に掛けると地面全体がざらざらして見えた。深度の差が大きい所はぼかさない
// ぼかし（バイラテラル）で、物の輪郭を残したままノイズだけをならしてから掛ける。
//
// 途中のバッファは、描いた直後に描画先から外してから SRV として読む。D3D11 は描画先に
// バインドしたままのテクスチャを SRV として読ませず 0 を返すので、合成で scene × 0 = 真っ黒になる。
// BloomEffect のダウン・アップサンプルと同じく、各回の後に OMSetRenderTargets(1, &nullRTV, nullptr) を呼ぶ
#include "SSAOEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

SSAOEffect::~SSAOEffect()
{
    ReleaseTargets();
}

void SSAOEffect::Initialize(ID3D11Device *device, UINT width, UINT height)
{
    m_res = std::make_unique<Resources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
                                       nullptr, 0, nullptr, m_res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOPS.cso",
                                      m_res->psRaw.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOBlurPS.cso",
                                      m_res->psBlur.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOCompositePS.cso",
                                      m_res->psComposite.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(device,
                                                  sizeof(GpuData::CbSSAOData), m_res->cbSSAO.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device,
                                                  sizeof(GpuData::CbSSAOBlurData), m_res->cbBlur.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, m_res->sampler.GetAddressOf());

    D3D11_BLEND_DESC blendDesc                      = {};
    blendDesc.RenderTarget[0].BlendEnable           = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr                                      = device->CreateBlendState(&blendDesc, m_opaqueBlendState.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), L"SSAOEffect: OpaqueBlendState 生成失敗");

    CreateTargets(width, height);
}

void SSAOEffect::CreateTargets(UINT width, UINT height)
{
    m_width  = width;
    m_height = height;
    m_rawAO  = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
    m_blurH  = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
    m_blurV  = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
}

void SSAOEffect::ReleaseTargets()
{
    if (m_rawAO.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_rawAO);
    if (m_blurH.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_blurH);
    if (m_blurV.IsValid()) ResourceManager::Instance().UnloadRenderTarget(m_blurV);
}

void SSAOEffect::Resize(ID3D11Device * /*device*/, UINT width, UINT height)
{
    ReleaseTargets();
    CreateTargets(width, height);
}

void SSAOEffect::CopyInputToOutput(ID3D11DeviceContext      *dc,
                                   ID3D11ShaderResourceView *inputSRV,
                                   ID3D11RenderTargetView   *outputRTV)
{
    if (!inputSRV || !outputRTV) return;

    ID3D11Resource *src = nullptr;
    ID3D11Resource *dst = nullptr;
    inputSRV->GetResource(&src);
    outputRTV->GetResource(&dst);
    if (src && dst) dc->CopyResource(dst, src);
    if (src) src->Release();
    if (dst) dst->Release();
}

void SSAOEffect::Process(ID3D11DeviceContext      *dc,
                         ID3D11ShaderResourceView *inputSRV,
                         ID3D11RenderTargetView   *outputRTV)
{
    if (!m_res || !inputSRV || !outputRTV) return;

    if (m_cbData.ssaoIntensity <= 0.0f)
    {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    RenderTarget *rawAO = ResourceManager::Instance().GetRenderTarget(m_rawAO);
    RenderTarget *blurH = ResourceManager::Instance().GetRenderTarget(m_blurH);
    RenderTarget *blurV = ResourceManager::Instance().GetRenderTarget(m_blurV);
    if (!rawAO || !blurH || !blurV || !rawAO->IsValid() || !blurH->IsValid() || !blurV->IsValid())
    {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_res->vs.Get(), nullptr, 0);
    dc->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);

    ID3D11SamplerState *smp = m_res->sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &smp);

    ID3D11RenderTargetView *nullRTV = nullptr;

    // 遮られる度合いを求める（深度 t4・法線 t6 → rawAO）
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_res->cbSSAO.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &m_cbData, sizeof(GpuData::CbSSAOData));
            dc->Unmap(m_res->cbSSAO.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_SSAO, 1, m_res->cbSSAO.GetAddressOf());
        dc->PSSetShader(m_res->psRaw.Get(), nullptr, 0);
        rawAO->Activate(dc);
        dc->Draw(3, 0);
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    auto runBlurPass = [&](ID3D11ShaderResourceView *input, RenderTarget *output, float stepX, float stepY) {
        GpuData::CbSSAOBlurData blurData{};
        blurData.blurTexelStep   = {stepX, stepY};
        blurData.blurDepthSigma  = m_blurDepthSigma;
        blurData.blurMaxDistance = m_cbData.ssaoMaxDistance;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(m_res->cbBlur.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            memcpy(mapped.pData, &blurData, sizeof(blurData));
            dc->Unmap(m_res->cbBlur.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_SSAO_BLUR, 1, m_res->cbBlur.GetAddressOf());
        dc->PSSetShader(m_res->psBlur.Get(), nullptr, 0);
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &input);
        output->Activate(dc);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    };

    // 横にぼかす（rawAO → blurH）
    runBlurPass(rawAO->GetSRV(), blurH, 1.0f / static_cast<float>(m_width), 0.0f);

    // 縦にぼかす（blurH → blurV）
    runBlurPass(blurH->GetSRV(), blurV, 0.0f, 1.0f / static_cast<float>(m_height));

    // シーンの色 × ぼかした値 → outputRTV
    {
        dc->PSSetShader(m_res->psComposite.Get(), nullptr, 0);
        dc->OMSetRenderTargets(1, &outputRTV, nullptr);

        ID3D11ShaderResourceView *srvs[] = {inputSRV, blurV->GetSRV()};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRVs[] = {nullptr, nullptr};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
    }
}
