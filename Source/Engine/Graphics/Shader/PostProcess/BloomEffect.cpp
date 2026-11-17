#include "BloomEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Core/Math/Misc.h"
#include <algorithm>

namespace
{
    // 途中で描けなくなったときに、入力をそのまま出力へ写す（出力を書かずに戻ると次のエフェクトが古い絵を読む）
    void CopyInputToOutput(ID3D11DeviceContext      *dc,
                           ID3D11ShaderResourceView *inputSRV,
                           ID3D11RenderTargetView   *outputRTV)
    {
        Microsoft::WRL::ComPtr<ID3D11Resource> src;
        Microsoft::WRL::ComPtr<ID3D11Resource> dst;
        inputSRV->GetResource(&src);
        outputRTV->GetResource(&dst);
        if (src && dst) dc->CopyResource(dst.Get(), src.Get());
    }
}

BloomEffect::~BloomEffect()
{
    ReleaseChains();
}



// ---------------------------------------------------------------------------
// 初期化とリソース
// ---------------------------------------------------------------------------

void BloomEffect::Initialize(ID3D11Device *device, UINT width, UINT height)
{
    m_res = std::make_unique<BloomResources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
                                       nullptr, 0, nullptr, m_res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomExtractionPS.cso",
                                      m_res->psExtract.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomDownsamplePS.cso",
                                      m_res->psDown.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomUpsamplePS.cso",
                                      m_res->psUp.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomCompositePS.cso",
                                      m_res->psComposite.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(GpuData::CbBloomData),
                                                  m_res->cbBloom.GetAddressOf());

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
    _ASSERT_EXPR(SUCCEEDED(hr), L"BloomEffect: OpaqueBlendState 生成失敗");

    CreateChains(width, height);
}



void BloomEffect::Resize(ID3D11Device * /*device*/, UINT width, UINT height)
{
    ReleaseChains();
    CreateChains(width, height);
}



void BloomEffect::CreateChains(UINT width, UINT height)
{
    m_downChain.reserve(kMaxIterations);
    m_upChain.reserve(kMaxIterations);

    UINT w = width, h = height;
    for (int i = 0; i < kMaxIterations; ++i)
    {
        w = (std::max)(1u, w / 2);
        h = (std::max)(1u, h / 2);
        m_downChain.push_back(ResourceManager::Instance().CreateRenderTarget(
            w, h, DXGI_FORMAT_R16G16B16A16_FLOAT));
        m_upChain.push_back(ResourceManager::Instance().CreateRenderTarget(
            w, h, DXGI_FORMAT_R16G16B16A16_FLOAT));
    }
}



void BloomEffect::ReleaseChains()
{
    for (auto h : m_downChain)
        ResourceManager::Instance().UnloadRenderTarget(h);
    for (auto h : m_upChain)
        ResourceManager::Instance().UnloadRenderTarget(h);

    m_downChain.clear();
    m_upChain.clear();
}



// ---------------------------------------------------------------------------
// 毎フレームの処理
// ---------------------------------------------------------------------------

void BloomEffect::UpdateCbBloom(ID3D11DeviceContext *dc, const GpuData::CbBloomData &data)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_res->cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &data, sizeof(data));
        dc->Unmap(m_res->cbBloom.Get(), 0);
    }
}



void BloomEffect::Process(
    ID3D11DeviceContext      *dc,
    ID3D11ShaderResourceView *inputSRV,
    ID3D11RenderTargetView   *outputRTV)
{
    if (!inputSRV) return;
    if (!m_res || m_downChain.empty())
    {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_res->vs.Get(), nullptr, 0);

    // パーティクルなどの加算ブレンドが残っていても、上書きで描く
    dc->OMSetBlendState(m_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);

    ID3D11SamplerState *samplers[] = {m_res->sampler.Get()};
    dc->PSSetSamplers(SLOT_SMP_BLOOM, 1, samplers);

    // 明るい所を抜き出す（inputSRV → m_downChain[0]）
    {
        RenderTarget *down0 = ResourceManager::Instance().GetRenderTarget(m_downChain[0]);
        if (!down0)
        {
            CopyInputToOutput(dc, inputSRV, outputRTV);
            return;
        }

        // テクセルの大きさは入力（フル解像度 = down0 の2倍）のもの
        cbData.texelWidth  = 1.0f / (down0->GetWidth() * 2.0f);
        cbData.texelHeight = 1.0f / (down0->GetHeight() * 2.0f);
        UpdateCbBloom(dc, cbData);
        dc->PSSetConstantBuffers(SLOT_CB_BLOOM, 1, m_res->cbBloom.GetAddressOf());

        dc->PSSetShader(m_res->psExtract.Get(), nullptr, 0);
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputSRV);
        down0->Activate(dc);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
    }

    // 縮小（m_downChain[i] → m_downChain[i + 1]）
    dc->PSSetShader(m_res->psDown.Get(), nullptr, 0);

    for (size_t i = 0; i < m_downChain.size() - 1; ++i)
    {
        RenderTarget *src = ResourceManager::Instance().GetRenderTarget(m_downChain[i]);
        RenderTarget *dst = ResourceManager::Instance().GetRenderTarget(m_downChain[i + 1]);
        if (!src || !dst) continue;

        cbData.texelWidth  = 1.0f / src->GetWidth();
        cbData.texelHeight = 1.0f / src->GetHeight();
        UpdateCbBloom(dc, cbData);

        dst->Activate(dc);
        ID3D11ShaderResourceView *srvs[] = {src->GetSRV()};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
        ID3D11RenderTargetView *nullRTV = nullptr;
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // 拡大（m_downChain の一番小さいもの → ... → m_upChain[0]）
    // 1つ大きい m_downChain と足しながら大きくしていく
    dc->PSSetShader(m_res->psUp.Get(), nullptr, 0);

    RenderTarget             *lastDown   = ResourceManager::Instance().GetRenderTarget(m_downChain.back());
    ID3D11ShaderResourceView *currentSRV = lastDown ? lastDown->GetSRV() : nullptr;

    for (int i = (int)m_downChain.size() - 2; i >= 0; --i)
    {
        RenderTarget *dst     = ResourceManager::Instance().GetRenderTarget(m_upChain[i]);
        RenderTarget *highRes = ResourceManager::Instance().GetRenderTarget(m_downChain[i]);
        if (!dst || !highRes) continue;

        // radius を掛けて、サンプルする間隔を広げる（にじみが大きくなる）
        cbData.texelWidth  = (1.0f / dst->GetWidth()) * cbData.radius;
        cbData.texelHeight = (1.0f / dst->GetHeight()) * cbData.radius;
        UpdateCbBloom(dc, cbData);

        dst->Activate(dc);
        ID3D11ShaderResourceView *srvs[] = {currentSRV, highRes->GetSRV()};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRVs[] = {nullptr, nullptr};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
        ID3D11RenderTargetView *nullRTV = nullptr;
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);

        currentSRV = dst->GetSRV();
    }

    RenderTarget *finalGlow = ResourceManager::Instance().GetRenderTarget(m_upChain[0]);
    if (!finalGlow)
    {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    // 合成（シーン + 光 → outputRTV）
    {
        UpdateCbBloom(dc, cbData);
        dc->PSSetConstantBuffers(SLOT_CB_BLOOM, 1, m_res->cbBloom.GetAddressOf());
        dc->PSSetShader(m_res->psComposite.Get(), nullptr, 0);

        // ビューポートが縮小バッファの大きさのままなので、出力と同じ大きさ（= 入力の大きさ）に戻す
        D3D11_TEXTURE2D_DESC                   desc;
        Microsoft::WRL::ComPtr<ID3D11Resource> res;
        inputSRV->GetResource(&res);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        res.As(&tex);
        tex->GetDesc(&desc);

        D3D11_VIEWPORT vp = {};
        vp.Width          = static_cast<float>(desc.Width);
        vp.Height         = static_cast<float>(desc.Height);
        vp.MaxDepth       = 1.0f;
        dc->RSSetViewports(1, &vp);

        dc->OMSetRenderTargets(1, &outputRTV, nullptr);

        ID3D11ShaderResourceView *srvs[] = {inputSRV, finalGlow->GetSRV()};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView *nullSRVs[] = {nullptr, nullptr};
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
        // 描画先から外すのは PostProcessManager が行う
    }
}