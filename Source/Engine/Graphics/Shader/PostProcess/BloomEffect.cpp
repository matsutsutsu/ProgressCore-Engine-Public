#include "BloomEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Core/Math/Misc.h"
#include <algorithm>

BloomEffect::~BloomEffect()
{
    ReleaseChains();
}

void BloomEffect::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    // ---- シェーダーリソース（旧 static g_bloomResources → メンバ _res）----
    _res = std::make_unique<BloomResources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
        nullptr, 0, nullptr, _res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomExtractionPS.cso",
        _res->psExtract.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomDownsamplePS.cso",
        _res->psDown.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomUpsamplePS.cso",
        _res->psUp.GetAddressOf());
    // 新規: scene + glow 合成シェーダー (BloomCompositePS.cso)
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/BloomCompositePS.cso",
        _res->psComposite.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(CbBloomData),
        _res->cbBloom.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, _res->sampler.GetAddressOf());

    // ---- Opaque BlendState（旧 Graphics::Instance() 依存を完全排除）----
    // パーティクル等の加算設定が残っている場合の安全リセット用
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device->CreateBlendState(&blendDesc, _opaqueBlendState.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), L"BloomEffect: OpaqueBlendState 生成失敗");

    CreateChains(width, height);
}

void BloomEffect::Resize(ID3D11Device* /*device*/, UINT width, UINT height)
{
    ReleaseChains();
    CreateChains(width, height);
}

void BloomEffect::CreateChains(UINT width, UINT height)
{
    _downChain.reserve(kMaxIterations);
    _upChain.reserve(kMaxIterations);

    UINT w = width, h = height;
    for (int i = 0; i < kMaxIterations; ++i) {
        w = (std::max)(1u, w / 2);
        h = (std::max)(1u, h / 2);
        _downChain.push_back(ResourceManager::Instance().CreateRenderTarget(
            w, h, DXGI_FORMAT_R16G16B16A16_FLOAT));
        _upChain.push_back(ResourceManager::Instance().CreateRenderTarget(
            w, h, DXGI_FORMAT_R16G16B16A16_FLOAT));
    }

    // 合成結果 RT はフルサイズ
    _compositeRT = ResourceManager::Instance().CreateRenderTarget(
        width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void BloomEffect::ReleaseChains()
{
    for (auto h : _downChain) ResourceManager::Instance().UnloadRenderTarget(h);
    for (auto h : _upChain)   ResourceManager::Instance().UnloadRenderTarget(h);
    if (_compositeRT.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_compositeRT);
    _downChain.clear();
    _upChain.clear();
    _compositeRT = {};
}

void BloomEffect::UpdateCbBloom(ID3D11DeviceContext* dc, const CbBloomData& data)
{
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(_res->cbBloom.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &data, sizeof(data));
        dc->Unmap(_res->cbBloom.Get(), 0);
    }
}

ID3D11ShaderResourceView* BloomEffect::Process(
    ID3D11DeviceContext* dc,
    ID3D11ShaderResourceView* inputSRV)
{
    if (!_res || _downChain.empty() || !inputSRV) return nullptr;

    // ---- 共通ステート ----
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_res->vs.Get(), nullptr, 0);

    // パーティクル等の加算ブレンドが残っている場合のリセット
    // 旧: Graphics::Instance().GetRenderState()->GetBlendState(BlendState::Opaque)
    // 新: 自前の OpaqueBlendState（Initialize 時に生成済み）
    dc->OMSetBlendState(_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);

    ID3D11SamplerState* samplers[] = { _res->sampler.Get() };
    dc->PSSetSamplers(SLOT_SMP_BLOOM, 1, samplers);

    CbBloomData cbData;
    cbData.threshold = threshold;
    cbData.softKnee = softKnee;
    cbData.intensity = intensity;
    cbData.padding = 0.f;

    // =================================================================
    // 1. 輝度抽出 (inputSRV → _downChain[0])
    // =================================================================
    {
        RenderTarget* down0 = ResourceManager::Instance().GetRenderTarget(_downChain[0]);
        if (!down0) return nullptr;

        cbData.texelWidth = 1.0f / (down0->GetWidth() * 2.0f);
        cbData.texelHeight = 1.0f / (down0->GetHeight() * 2.0f);
        UpdateCbBloom(dc, cbData);
        dc->PSSetConstantBuffers(SLOT_CB_BLOOM, 1, _res->cbBloom.GetAddressOf());

        dc->PSSetShader(_res->psExtract.Get(), nullptr, 0);
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputSRV);
        down0->Activate(dc);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
    }

    // =================================================================
    // 2. ダウンサンプリング (_downChain[i] → _downChain[i+1])
    // =================================================================
    dc->PSSetShader(_res->psDown.Get(), nullptr, 0);

    for (size_t i = 0; i < _downChain.size() - 1; ++i) {
        RenderTarget* src = ResourceManager::Instance().GetRenderTarget(_downChain[i]);
        RenderTarget* dst = ResourceManager::Instance().GetRenderTarget(_downChain[i + 1]);
        if (!src || !dst) continue;

        cbData.texelWidth = 1.0f / src->GetWidth();
        cbData.texelHeight = 1.0f / src->GetHeight();
        UpdateCbBloom(dc, cbData);

        dst->Activate(dc);
        ID3D11ShaderResourceView* srvs[] = { src->GetSRV() };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
        ID3D11RenderTargetView* nullRTV = nullptr;
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    // =================================================================
    // 3. アップサンプリング (_downChain[last] → ... → _upChain[0])
    // =================================================================
    dc->PSSetShader(_res->psUp.Get(), nullptr, 0);

    RenderTarget* lastDown = ResourceManager::Instance().GetRenderTarget(_downChain.back());
    ID3D11ShaderResourceView* currentSRV = lastDown ? lastDown->GetSRV() : nullptr;

    for (int i = (int)_downChain.size() - 2; i >= 0; --i) {
        RenderTarget* dst = ResourceManager::Instance().GetRenderTarget(_upChain[i]);
        RenderTarget* highRes = ResourceManager::Instance().GetRenderTarget(_downChain[i]);
        if (!dst || !highRes) continue;

        cbData.texelWidth = (1.0f / dst->GetWidth()) * radius;
        cbData.texelHeight = (1.0f / dst->GetHeight()) * radius;
        UpdateCbBloom(dc, cbData);

        dst->Activate(dc);
        ID3D11ShaderResourceView* srvs[] = { currentSRV, highRes->GetSRV() };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
        ID3D11RenderTargetView* nullRTV = nullptr;
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);

        currentSRV = dst->GetSRV();
    }

    // _upChain[0] = bloom グロー (half-res)
    RenderTarget* finalGlow = ResourceManager::Instance().GetRenderTarget(_upChain[0]);
    if (!finalGlow) return nullptr;

    // =================================================================
    // 4. 合成 (scene + glow → _compositeRT, フル解像度 HDR)
    //    旧: Tone Mapper が 2 入力 (scene, glow) で合成していた
    //    新: BloomEffect が合成済み HDR を返す → Tone Mapper は 1 入力
    //    ハードウェアのバイリニアフィルタが half-res グローを自動補間
    // =================================================================
    {
        RenderTarget* compRT = ResourceManager::Instance().GetRenderTarget(_compositeRT);
        if (!compRT) return nullptr;

        // cbData.intensity がシェーダーで参照される
        UpdateCbBloom(dc, cbData);
        dc->PSSetConstantBuffers(SLOT_CB_BLOOM, 1, _res->cbBloom.GetAddressOf());

        dc->PSSetShader(_res->psComposite.Get(), nullptr, 0);
        compRT->Activate(dc);

        // t0 = 元シーン HDR, t1 = ブルームグロー (half-res → GPU が bilinear で補間)
        ID3D11ShaderResourceView* srvs[] = { inputSRV, finalGlow->GetSRV() };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
        ID3D11RenderTargetView* nullRTV = nullptr;
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    RenderTarget* compRT = ResourceManager::Instance().GetRenderTarget(_compositeRT);
    return compRT ? compRT->GetSRV() : nullptr;
}