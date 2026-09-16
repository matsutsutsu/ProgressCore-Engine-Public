// RadialBlurEffect の実装。フルスクリーン1パスで放射状ブラーを適用する。
// Why: 中間RTを介さず inputSRV→outputRTV へ直接描く。ping-pong により
// 両者は必ず別テクスチャなので read/write 競合は起きず、Distortion のような
// 中間RT+CopyResource(帯域2倍)を避けられる。
#include "RadialBlurEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

void RadialBlurEffect::Initialize(ID3D11Device *device, UINT /*width*/, UINT /*height*/)
{
    m_res = std::make_unique<Resources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
                                       nullptr, 0, nullptr, m_res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/RadialBlurPS.cso",
                                      m_res->ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device,
                                                  sizeof(GpuData::CbRadialBlurData), m_res->cb.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter             = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV           = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW           = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, m_res->sampler.GetAddressOf());
}

void RadialBlurEffect::Resize(ID3D11Device * /*device*/, UINT /*width*/, UINT /*height*/)
{
    // 中間RTを持たないため何もしない（入力→出力を直接描くだけ）。
}

void RadialBlurEffect::Process(ID3D11DeviceContext      *dc,
                               ID3D11ShaderResourceView *inputSRV,
                               ID3D11RenderTargetView   *outputRTV)
{
    if (!m_res || m_cbData.blurStrength <= 0.0f)
    {
        ID3D11Resource *src = nullptr;
        ID3D11Resource *dst = nullptr;
        inputSRV->GetResource(&src);
        outputRTV->GetResource(&dst);
        if (src && dst) dc->CopyResource(dst, src);
        if (src) src->Release();
        if (dst) dst->Release();
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_res->cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &m_cbData, sizeof(GpuData::CbRadialBlurData));
        dc->Unmap(m_res->cb.Get(), 0);
    }

    dc->OMSetRenderTargets(1, &outputRTV, nullptr);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(m_res->vs.Get(), nullptr, 0);
    dc->PSSetShader(m_res->ps.Get(), nullptr, 0);

    ID3D11SamplerState *smp = m_res->sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &smp);
    dc->PSSetConstantBuffers(SLOT_CB_RADIALBLUR, 1, m_res->cb.GetAddressOf());
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputSRV);

    dc->Draw(3, 0);

    ID3D11ShaderResourceView *nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
}
