#include "ToneMapEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"

void ToneMapEffect::Initialize(ID3D11Device* device)
{
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso", nullptr, 0, nullptr, _vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ToneMapPS.cso", _ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(CbToneMap), _cbToneMap.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, _sampler.GetAddressOf());
}

void ToneMapEffect::Process(ID3D11DeviceContext* dc,
    ID3D11ShaderResourceView* inputHDR,
    ID3D11RenderTargetView* outputSDR,
    ToneMapConfigComponent& config)
{
    // 出力先をバインド (Viewport は Manager 側で設定済み)
    dc->OMSetRenderTargets(1, &outputSDR, nullptr);

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_vs.Get(), nullptr, 0);
    dc->PSSetShader(_ps.Get(), nullptr, 0);

    ID3D11SamplerState* smp = _sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_DEFAULT, 1, &smp);

    CbToneMap cbData{
        config.exposure, 
        config.chromaticAberration,
        {0.0f, 0.0f},
        {config.colorTint.x, config.colorTint.y, config.colorTint.z, 0.0f}
    };
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(_cbToneMap.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &cbData, sizeof(cbData));
        dc->Unmap(_cbToneMap.Get(), 0);
    }
    dc->PSSetConstantBuffers(SLOT_CB_TONEMAP, 1, _cbToneMap.GetAddressOf());

    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputHDR);
    dc->Draw(3, 0);

    // クリーンアップ
    ID3D11ShaderResourceView* nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
}