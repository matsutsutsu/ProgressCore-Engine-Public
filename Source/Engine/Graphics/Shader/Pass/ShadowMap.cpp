#include "ShadowMap.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/GpuResourceUtils.h"
#include "Engine/Graphics/RHI/DebugName.h"

ShadowMap::ShadowMap(ID3D11Device *device, UINT width, UINT height)
{
    HRESULT hr = S_OK;

    this->width  = width;
    this->height = height;

    // DSV（D24S8）と SRV（R24）の両方で見るので、形式は TYPELESS で作る
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width              = width;
    desc.Height             = height;
    desc.MipLevels          = 1;
    desc.ArraySize          = SHADOW_CASCADE_COUNT;
    desc.Format             = DXGI_FORMAT_R24G8_TYPELESS;
    desc.SampleDesc.Count   = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    // テクスチャは DSV と SRV が参照を持つので、ここではローカルに持つだけでよい
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthBuffer;
    hr = device->CreateTexture2D(&desc, nullptr, depthBuffer.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    // 書き込み用に、カスケード1枚ずつを指す DSV を作る
    depthStencilViews.resize(SHADOW_CASCADE_COUNT);
    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format                         = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension                  = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize       = 1;

        hr = device->CreateDepthStencilView(depthBuffer.Get(), &dsvDesc, depthStencilViews[i].GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // 読み込み用に、全カスケードをまとめて指す SRV を作る
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format                         = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension                  = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels       = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize       = SHADOW_CASCADE_COUNT;

    hr = device->CreateShaderResourceView(depthBuffer.Get(), &srvDesc, shaderResourceView.GetAddressOf());
    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

    SetDebugName(shaderResourceView.Get(), "ShadowMap_SRV");

    // 比較サンプラー。シャドウマップの外は境界色 1.0（一番奥）を返し、影にならない
    D3D11_SAMPLER_DESC sampDesc{};
    sampDesc.Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.AddressW       = D3D11_TEXTURE_ADDRESS_BORDER;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
    sampDesc.BorderColor[0] = 1.0f;
    sampDesc.BorderColor[1] = 1.0f;
    sampDesc.BorderColor[2] = 1.0f;
    sampDesc.BorderColor[3] = 1.0f;
    device->CreateSamplerState(&sampDesc, samplerState.GetAddressOf());

    GpuResourceUtils::CreateDynamicConstantBuffer(device, sizeof(GpuData::CbShadow), constantBuffer.GetAddressOf());

    viewport.Width    = (float)width;
    viewport.Height   = (float)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
}

void ShadowMap::Activate(ID3D11DeviceContext *dc, int cascadeIndex)
{
    dc->ClearDepthStencilView(depthStencilViews[cascadeIndex].Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    dc->RSSetViewports(1, &viewport);
    dc->OMSetRenderTargets(0, nullptr, depthStencilViews[cascadeIndex].Get());
}

void ShadowMap::Deactivate(ID3D11DeviceContext *dc)
{
    dc->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowMap::Bind(ID3D11DeviceContext *dc, int srvSlot, int samplerSlot, int cbSlot)
{
    if (srvSlot >= 0)
    {
        ID3D11ShaderResourceView *srvs[] = {shaderResourceView.Get()};
        dc->PSSetShaderResources((UINT)srvSlot, 1, srvs);
    }

    if (samplerSlot >= 0)
    {
        ID3D11SamplerState *samplers[] = {samplerState.Get()};
        dc->PSSetSamplers((UINT)samplerSlot, 1, samplers);
    }

    if (constantBuffer && cbSlot >= 0)
    {
        ID3D11Buffer *cbs[] = {constantBuffer.Get()};
        // 頂点シェーダーはライトの行列で座標を変え、ピクセルシェーダーは影の色・濃さを読む
        dc->VSSetConstantBuffers((UINT)cbSlot, 1, cbs);
        dc->PSSetConstantBuffers((UINT)cbSlot, 1, cbs);
    }
}

void ShadowMap::UpdateConstantBuffer(ID3D11DeviceContext *dc, const GpuData::CbShadow &data)
{
    if (!constantBuffer) return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = dc->Map(
        constantBuffer.Get(),
        0,
        D3D11_MAP_WRITE_DISCARD,
        0,
        &mappedResource);

    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, &data, sizeof(GpuData::CbShadow));
        dc->Unmap(constantBuffer.Get(), 0);
    }
    else
    {
        OutputDebugStringA("SHADOWMAP CONSTANT BUFFER MAP FAILED\n");
    }
}
