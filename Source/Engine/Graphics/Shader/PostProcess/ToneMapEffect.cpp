#include "ToneMapEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Components/PostProcess/ToneMapConfigComponent.h"

void ToneMapEffect::Initialize(ID3D11Device *device)
{
    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso", nullptr, 0, nullptr, m_vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ToneMapPS.cso", m_ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(GpuData::CbToneMapData), m_cbToneMap.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, m_sampler.GetAddressOf());
}



void ToneMapEffect::Process(ID3D11DeviceContext      *dc,
                            ID3D11ShaderResourceView *inputHDR,
                            ID3D11RenderTargetView   *outputSDR,
                            ToneMapConfigComponent   &config)
{
    dc->OMSetRenderTargets(1, &outputSDR, nullptr);

    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_vs.Get(), nullptr, 0);
    dc->PSSetShader(m_ps.Get(), nullptr, 0);

    ID3D11SamplerState *smp = m_sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_DEFAULT, 1, &smp);

    GpuData::CbToneMapData cbData{
        config.data.exposure,
        config.data.chromaticAberration,
        {0.0f, 0.0f},
        {config.data.colorTint.x, config.data.colorTint.y, config.data.colorTint.z, 0.0f}};
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_cbToneMap.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &cbData, sizeof(cbData));
        dc->Unmap(m_cbToneMap.Get(), 0);
    }
    dc->PSSetConstantBuffers(SLOT_CB_TONEMAP, 1, m_cbToneMap.GetAddressOf());

    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputHDR);
    dc->Draw(3, 0);

    ID3D11ShaderResourceView *nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
}