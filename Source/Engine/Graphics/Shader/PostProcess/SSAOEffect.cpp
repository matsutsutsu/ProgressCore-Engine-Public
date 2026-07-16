/// @file  SSAOEffect.cpp
/// @brief SSAOEffect の実装(4パス: 遮蔽値計算→深度考慮ブラーH/V→合成)。
///
///  Why 4パスなのか:
///    半球サンプリング由来のノイズを、ぼかし無しでそのまま最終カラーに乗算していたため
///    「地面全体がザラザラする」症状が出ていた。BloomEffectと同じ「多段パスで
///    品質を作ってから最後に合成する」構成に変更し、深度考慮ブラー(バイラテラル)で
///    輪郭を保ったままノイズだけ均す。
///
///  ★重要: 各中間RTは「RTVとして書いた直後」に解除してから「SRVとして読む」。
///    D3D11ではRTVバインドが残ったままSRVとして読もうとするとハザード検知で
///    強制的に0埋めされる(合成で scene * 0 = 真っ黒になる)。
///    BloomEffect.cpp のダウン/アップサンプルループと同じく、各パス直後に
///    OMSetRenderTargets(1, &nullRTV, nullptr) を挟む。
#include "SSAOEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

SSAOEffect::~SSAOEffect()
{
    ReleaseTargets();
}

void SSAOEffect::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    _res = std::make_unique<Resources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
        nullptr, 0, nullptr, _res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOPS.cso",
        _res->psRaw.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOBlurPS.cso",
        _res->psBlur.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/SSAOCompositePS.cso",
        _res->psComposite.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(device,
        sizeof(GpuData::CbSSAOData), _res->cbSSAO.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device,
        sizeof(GpuData::CbSSAOBlurData), _res->cbBlur.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, _res->sampler.GetAddressOf());

    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device->CreateBlendState(&blendDesc, _opaqueBlendState.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), L"SSAOEffect: OpaqueBlendState 生成失敗");

    CreateTargets(width, height);
}

void SSAOEffect::CreateTargets(UINT width, UINT height)
{
    _width = width;
    _height = height;
    _rawAO = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
    _blurH = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
    _blurV = ResourceManager::Instance().CreateRenderTarget(width, height, DXGI_FORMAT_R8_UNORM);
}

void SSAOEffect::ReleaseTargets()
{
    if (_rawAO.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_rawAO);
    if (_blurH.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_blurH);
    if (_blurV.IsValid()) ResourceManager::Instance().UnloadRenderTarget(_blurV);
}

void SSAOEffect::Resize(ID3D11Device* /*device*/, UINT width, UINT height)
{
    ReleaseTargets();
    CreateTargets(width, height);
}

void SSAOEffect::CopyInputToOutput(ID3D11DeviceContext* dc,
                                   ID3D11ShaderResourceView* inputSRV,
                                   ID3D11RenderTargetView* outputRTV)
{
    if (!inputSRV || !outputRTV) return;

    ID3D11Resource* src = nullptr;
    ID3D11Resource* dst = nullptr;
    inputSRV->GetResource(&src);
    outputRTV->GetResource(&dst);
    if (src && dst) dc->CopyResource(dst, src);
    if (src) src->Release();
    if (dst) dst->Release();
}

void SSAOEffect::Process(ID3D11DeviceContext* dc,
                         ID3D11ShaderResourceView* inputSRV,
                         ID3D11RenderTargetView* outputRTV)
{
    if (!_res || !inputSRV || !outputRTV) return;

    if (_cbData.ssaoIntensity <= 0.0f) {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    RenderTarget* rawAO = ResourceManager::Instance().GetRenderTarget(_rawAO);
    RenderTarget* blurH = ResourceManager::Instance().GetRenderTarget(_blurH);
    RenderTarget* blurV = ResourceManager::Instance().GetRenderTarget(_blurV);
    if (!rawAO || !blurH || !blurV || !rawAO->IsValid() || !blurH->IsValid() || !blurV->IsValid()) {
        CopyInputToOutput(dc, inputSRV, outputRTV);
        return;
    }

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_res->vs.Get(), nullptr, 0);
    dc->OMSetBlendState(_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);

    ID3D11SamplerState* smp = _res->sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &smp);

    ID3D11RenderTargetView* nullRTV = nullptr;

    // ① 遮蔽値計算 (深度t4+法線t6 → rawAO)
    {
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(_res->cbSSAO.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &_cbData, sizeof(GpuData::CbSSAOData));
            dc->Unmap(_res->cbSSAO.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_SSAO, 1, _res->cbSSAO.GetAddressOf());
        dc->PSSetShader(_res->psRaw.Get(), nullptr, 0);
        rawAO->Activate(dc);
        dc->Draw(3, 0);
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    auto runBlurPass = [&](ID3D11ShaderResourceView* input, RenderTarget* output, float stepX, float stepY) {
        GpuData::CbSSAOBlurData blurData{};
        blurData.blurTexelStep = { stepX, stepY };
        blurData.blurDepthSigma = _blurDepthSigma;
        blurData.blurMaxDistance = _cbData.ssaoMaxDistance;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(dc->Map(_res->cbBlur.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, &blurData, sizeof(blurData));
            dc->Unmap(_res->cbBlur.Get(), 0);
        }
        dc->PSSetConstantBuffers(SLOT_CB_SSAO_BLUR, 1, _res->cbBlur.GetAddressOf());
        dc->PSSetShader(_res->psBlur.Get(), nullptr, 0);
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &input);
        output->Activate(dc);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
        dc->OMSetRenderTargets(1, &nullRTV, nullptr);
    };

    // ② ブラー H (rawAO → blurH)
    runBlurPass(rawAO->GetSRV(), blurH, 1.0f / static_cast<float>(_width), 0.0f);

    // ③ ブラー V (blurH → blurV)
    runBlurPass(blurH->GetSRV(), blurV, 0.0f, 1.0f / static_cast<float>(_height));

    // ④ 合成 (シーン色 × ぼかし済みAO → outputRTV)
    {
        dc->PSSetShader(_res->psComposite.Get(), nullptr, 0);
        dc->OMSetRenderTargets(1, &outputRTV, nullptr);

        ID3D11ShaderResourceView* srvs[] = { inputSRV, blurV->GetSRV() };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, srvs);
        dc->Draw(3, 0);

        ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr };
        dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 2, nullSRVs);
    }
}
