#include "DistortionEffect.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

DistortionEffect::~DistortionEffect() {
    if (m_intermediateRT.IsValid()) {
        ResourceManager::Instance().UnloadRenderTarget(m_intermediateRT);
    }
}

void DistortionEffect::Initialize(ID3D11Device* device, UINT width, UINT height) {
    m_res = std::make_unique<Resources>();

    GpuResourceUtils::LoadVertexShader(device, "Assets/Shader/FullscreenVS.cso", nullptr, 0, nullptr, m_res->vs.GetAddressOf());
    GpuResourceUtils::LoadPixelShader(device, "Assets/Shader/DistortionPS.cso", m_res->ps.GetAddressOf());
    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(GpuData::CbDistortionData), m_res->cbDistortion.GetAddressOf());

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&sampDesc, m_res->sampler.GetAddressOf());

    Resize(device, width, height);
}

void DistortionEffect::Resize(ID3D11Device* device, UINT width, UINT height) {
    if (m_intermediateRT.IsValid()) {
        ResourceManager::Instance().UnloadRenderTarget(m_intermediateRT);
    }
    // 中間描画用バッファ（HDRフォーマット）
    m_intermediateRT = ResourceManager::Instance().LoadRenderTarget("DistortionRT", device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void DistortionEffect::Process(ID3D11DeviceContext* dc, ID3D11ShaderResourceView* inputSRV, ID3D11RenderTargetView* outputRTV) {
    // 描画不要な場合は「CopyResource」でスルーさせる（帯域の大幅な節約）
    if (!m_enabled || m_cbData.strength <= 0.0f) {
        ID3D11Resource* srcRes = nullptr;
        ID3D11Resource* dstRes = nullptr;
        inputSRV->GetResource(&srcRes);
        outputRTV->GetResource(&dstRes);
        if (srcRes && dstRes) dc->CopyResource(dstRes, srcRes);
        if (srcRes) srcRes->Release();
        if (dstRes) dstRes->Release();
        return;
    }

    // 定数バッファの更新
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(dc->Map(m_res->cbDistortion.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped.pData, &m_cbData, sizeof(GpuData::CbDistortionData));
        dc->Unmap(m_res->cbDistortion.Get(), 0);
    }

    // 中間レンダーターゲットの取得
    RenderTarget* target = ResourceManager::Instance().GetRenderTarget(m_intermediateRT);
    if (!target) return;

    // 1. 中間RTに歪ませた絵を描画
    target->Activate(dc);
    dc->IASetInputLayout(nullptr);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    dc->VSSetShader(m_res->vs.Get(), nullptr, 0);
    dc->PSSetShader(m_res->ps.Get(), nullptr, 0);

    ID3D11SamplerState* samplers[] = { m_res->sampler.Get() };
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);
    dc->PSSetConstantBuffers(SLOT_CB_DISTORTION, 1, m_res->cbDistortion.GetAddressOf());
    dc->PSSetShaderResources(SLOT_SRV_PP_DISTORTION_MAP, 1, &inputSRV);

    dc->Draw(3, 0); // Fullscreen Quad

    // SRVの解除
    ID3D11ShaderResourceView* nullSRV = nullptr;
    dc->PSSetShaderResources(SLOT_SRV_PP_DISTORTION_MAP, 1, &nullSRV);

    // 2. 中間バッファの結果を要求された出力先(outputRTV)へコピー
    ID3D11Resource* srcRT = nullptr;
    ID3D11Resource* dstRT = nullptr;
    target->GetSRV()->GetResource(&srcRT);
    outputRTV->GetResource(&dstRT);
    if (srcRT && dstRT) dc->CopyResource(dstRT, srcRT);
    if (srcRT) srcRT->Release();
    if (dstRT) dstRT->Release();
}