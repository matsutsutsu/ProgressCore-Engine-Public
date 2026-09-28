#include "Light.h"
#include "GpuResourceUtils.h"
#include <string>

void LightManager::Initialize(ID3D11Device *device)
{
    HRESULT hr = GpuResourceUtils::CreateDynamicConstantBuffer(
        device,
        sizeof(LightConstantBuffer),
        constantBuffer.GetAddressOf());

    if (FAILED(hr))
    {
        OutputDebugStringA("FAILED TO CREATE LIGHT CONSTANT BUFFER\n");
    }
}

void LightManager::Bind(ID3D11DeviceContext *dc, int slot)
{
    if (!constantBuffer || slot < 0) return;

    // 1. vector の中身を、定数バッファの固定長配列に詰める
    cbData.gPointLightCount = static_cast<int>(pointLights.size());
    for (int i = 0; i < cbData.gPointLightCount; ++i)
    {
        cbData.gPointLights[i] = pointLights[i];
    }

    cbData.gSpotLightCount = static_cast<int>(spotLights.size());
    for (int i = 0; i < cbData.gSpotLightCount; ++i)
    {
        cbData.gSpotLights[i] = spotLights[i];
    }

    // 2. GPU へ送る。バッファは DYNAMIC なので Map / Unmap で書く
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT                  hr = dc->Map(constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, &cbData, sizeof(LightConstantBuffer));
        dc->Unmap(constantBuffer.Get(), 0);
    }
    else
    {
        // Map が失敗するなら、バッファが DYNAMIC で作られていない可能性がある
        OutputDebugStringA("MAP FAILED\n");
    }

    // 3. ピクセルシェーダーの slot 番にバインドする
    ID3D11Buffer *buffers[] = {constantBuffer.Get()};
    dc->PSSetConstantBuffers((UINT)slot, 1, buffers);
}
