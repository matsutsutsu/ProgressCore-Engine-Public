#include "PostProcessManager.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

struct CbToneMap {
    float exposure;
    float padding[3];
};

void PostProcessManager::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    _device = device;

    _toneMapRes = std::make_unique<ToneMapResources>();
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
        nullptr, 0, nullptr, _toneMapRes->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ToneMapPS.cso",
        _toneMapRes->ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(CbToneMap),
        _toneMapRes->cbToneMap.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, _toneMapRes->sampler.GetAddressOf());

    Resize(device, width, height);
}

PostProcessManager::~PostProcessManager()
{
    _effects.clear(); // 各 IPostEffect が自前の RT を解放する

    if (_sceneHDR.IsValid())  ResourceManager::Instance().UnloadRenderTarget(_sceneHDR);
    if (_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_outputSDR);
}

void PostProcessManager::Resize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0) return;
    _width = width;
    _height = height;

    if (_sceneHDR.IsValid())  ResourceManager::Instance().UnloadRenderTarget(_sceneHDR);
    if (_outputSDR.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_outputSDR);

    _sceneHDR = ResourceManager::Instance().CreateRenderTarget(
        width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    _outputSDR = ResourceManager::Instance().CreateRenderTarget(
        width, height, DXGI_FORMAT_R8G8B8A8_UNORM);

    // 全エフェクトの中間 RT もリサイズ
    for (auto& effect : _effects) {
        effect->Resize(device, width, height);
    }
}

void PostProcessManager::BeginScene(ID3D11DeviceContext* dc,
    const float          clearColor[4],
    ID3D11DepthStencilView* dsv)
{
    RenderTarget* sceneHDR = ResourceManager::Instance().GetRenderTarget(_sceneHDR);
    if (!sceneHDR) return;

    float w = static_cast<float>(sceneHDR->GetWidth());
    float h = static_cast<float>(sceneHDR->GetHeight());
    if (w <= 0.0f || w > 16384.0f || h <= 0.0f || h > 16384.0f) return;

    sceneHDR->Clear(dc, clearColor);

    ID3D11RenderTargetView* rtv = sceneHDR->GetRTV();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp = {};
    vp.Width = w;
    vp.Height = h;
    vp.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &vp);
}

ID3D11ShaderResourceView* PostProcessManager::EndScene(ID3D11DeviceContext* dc,
    ID3D11RenderTargetView* targetRTV)
{
    RenderTarget* sceneHDR = ResourceManager::Instance().GetRenderTarget(_sceneHDR);
    RenderTarget* outputSDR = ResourceManager::Instance().GetRenderTarget(_outputSDR);
    if (!sceneHDR || !outputSDR) return nullptr;

    float outW = static_cast<float>(outputSDR->GetWidth());
    float outH = static_cast<float>(outputSDR->GetHeight());
    if (outW <= 0.0f || outW > 16384.0f || outH <= 0.0f || outH > 16384.0f) return nullptr;

    // HDR RT を SRV として読むために RTV 登録を解除
    ID3D11RenderTargetView* nullRTV = nullptr;
    dc->OMSetRenderTargets(1, &nullRTV, nullptr);

    // ================================================================
    // エフェクトチェーン実行（BloomEffect を含む全 IPostEffect）
    //   - sceneHDR から始まり、各エフェクトが processedHDR を更新する
    //   - BloomEffect は「scene + glow 合成済み HDR」を返すので
    //     ToneMap は 1 入力のみで完結する
    // ================================================================
    ID3D11ShaderResourceView* processedHDR = sceneHDR->GetSRV();

    for (auto& effect : _effects) {
        if (!effect->IsEnabled()) continue;
        if (auto* result = effect->Process(dc, processedHDR)) {
            processedHDR = result;
        }
        // nullptr が返った場合はそのエフェクトをスキップして引き継ぐ
    }

    // ================================================================
    // ToneMapping（1 入力のみ）
    //   旧: {processedHDR, bloomSRV} の 2 入力
    //   新: {processedHDR} の 1 入力（BloomEffect が合成済み）
    // ================================================================
    if (targetRTV) {
        dc->OMSetRenderTargets(1, &targetRTV, nullptr);

        D3D11_VIEWPORT vp = {};
        vp.Width = outW;
        vp.Height = outH;
        vp.MaxDepth = 1.0f;
        dc->RSSetViewports(1, &vp);
    }
    else {
        outputSDR->Activate(dc);
    }

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_toneMapRes->vs.Get(), nullptr, 0);
    dc->PSSetShader(_toneMapRes->ps.Get(), nullptr, 0);

    ID3D11SamplerState* smp = _toneMapRes->sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_DEFAULT, 1, &smp);

    CbToneMap cbData{ toneMapConfig.exposure };
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(_toneMapRes->cbToneMap.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &cbData, sizeof(cbData));
        dc->Unmap(_toneMapRes->cbToneMap.Get(), 0);
    }
    dc->PSSetConstantBuffers(SLOT_CB_TONEMAP, 1, _toneMapRes->cbToneMap.GetAddressOf());

    // ★ 1 入力のみ（旧: 2 入力）
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &processedHDR);
    dc->Draw(3, 0);

    // クリーンアップ
    ID3D11ShaderResourceView* nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);

    return outputSDR->GetSRV();
}