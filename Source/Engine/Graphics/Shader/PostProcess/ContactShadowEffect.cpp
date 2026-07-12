/// @file  ContactShadowEffect.cpp
/// @brief ContactShadowEffect の実装。深度(t4)を光源方向へレイマーチする。
#include "ContactShadowEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

void ContactShadowEffect::Initialize(ID3D11Device* device, UINT /*width*/, UINT /*height*/)
{
    _res = std::make_unique<Resources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso",
        nullptr, 0, nullptr, _res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/ContactShadowPS.cso",
        _res->ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device,
        sizeof(GpuData::CbContactShadowData), _res->cb.GetAddressOf());

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
    _ASSERT_EXPR(SUCCEEDED(hr), L"ContactShadowEffect: OpaqueBlendState 生成失敗");
}

void ContactShadowEffect::Resize(ID3D11Device* /*device*/, UINT /*width*/, UINT /*height*/)
{
}

void ContactShadowEffect::Process(ID3D11DeviceContext* dc,
                                  ID3D11ShaderResourceView* inputSRV,
                                  ID3D11RenderTargetView* outputRTV)
{
    if (!_res || _cbData.csIntensity <= 0.0f) {
        ID3D11Resource* src = nullptr;
        ID3D11Resource* dst = nullptr;
        inputSRV->GetResource(&src);
        outputRTV->GetResource(&dst);
        if (src && dst) dc->CopyResource(dst, src);
        if (src) src->Release();
        if (dst) dst->Release();
        return;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(_res->cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &_cbData, sizeof(GpuData::CbContactShadowData));
        dc->Unmap(_res->cb.Get(), 0);
    }

    dc->OMSetRenderTargets(1, &outputRTV, nullptr);
    dc->OMSetBlendState(_opaqueBlendState.Get(), nullptr, 0xFFFFFFFF);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    dc->VSSetShader(_res->vs.Get(), nullptr, 0);
    dc->PSSetShader(_res->ps.Get(), nullptr, 0);

    ID3D11SamplerState* smp = _res->sampler.Get();
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, &smp);
    dc->PSSetConstantBuffers(SLOT_CB_CONTACTSHADOW, 1, _res->cb.GetAddressOf());
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &inputSRV);

    dc->Draw(3, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_BLOOM_IN, 1, &nullSRV);
}
