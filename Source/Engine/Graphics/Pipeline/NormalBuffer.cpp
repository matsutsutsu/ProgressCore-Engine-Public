/// @file  NormalBuffer.cpp
/// @brief NormalBuffer の実装。
#include "NormalBuffer.h"
#include "Engine/Platform/Core/Logger.h"

bool NormalBuffer::Create(ID3D11Device* device, UINT width, UINT height)
{
    if (!device || width == 0 || height == 0) return false;

    _width = width;
    _height = height;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R16G16_FLOAT;
    desc.SampleDesc = { 1, 0 };
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&desc, nullptr, _texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render, "[NormalBuffer] テクスチャ生成に失敗しました。");
        return false;
    }

    hr = device->CreateRenderTargetView(_texture.Get(), nullptr, _rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render, "[NormalBuffer] RTV生成に失敗しました。");
        return false;
    }

    hr = device->CreateShaderResourceView(_texture.Get(), nullptr, _srv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render, "[NormalBuffer] SRV生成に失敗しました。");
        return false;
    }

    return true;
}

void NormalBuffer::Resize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == _width && height == _height) return;
    Create(device, width, height);
}

void NormalBuffer::Activate(ID3D11DeviceContext* dc, ID3D11DepthStencilView* dsv)
{
    if (!dc || !_rtv) return;

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    dc->ClearRenderTargetView(_rtv.Get(), clear);

    ID3D11RenderTargetView* rtv = _rtv.Get();
    dc->OMSetRenderTargets(1, &rtv, dsv);

    D3D11_VIEWPORT vp{};
    vp.Width = static_cast<float>(_width);
    vp.Height = static_cast<float>(_height);
    vp.MaxDepth = 1.0f;
    dc->RSSetViewports(1, &vp);
}
