#include "RenderTarget.h"

// 既存のロガーを使用。CCL_LOG_ERROR / LogCategory がプロジェクト側で定義されている前提。
// 別ロガーを使う場合はここを差し替える。
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/Graphics.h"

RenderTarget::~RenderTarget()
{
    Release();
}

void RenderTarget::Release()
{
    _texture.Reset();
    _rtv.Reset();
    _srv.Reset();
    _width = 0;
    _height = 0;
    _format = DXGI_FORMAT_UNKNOWN;
}

bool RenderTarget::Create(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
    if (!device || width == 0 || height == 0) {
        CCL_LOG_ERROR(LogCategory::Render,
            "RenderTarget::Create invalid args (w=%u h=%u)", width, height);
        return false;
    }

    // 既存リソースを確実に解放してから再生成
    Release();
    _width = width;
    _height = height;
    _format = format;

    // 1. テクスチャ
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr,
        _texture.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render,
            "CreateTexture2D failed: hr=0x%08X w=%u h=%u", hr, width, height);
        Release();
        return false;
    }

    // 2. RTV
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = format;
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;

    hr = device->CreateRenderTargetView(_texture.Get(), &rtvDesc,
        _rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render, "CreateRTV failed: hr=0x%08X", hr);
        Release();
        return false;
    }

    // 3. SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(_texture.Get(), &srvDesc,
        _srv.ReleaseAndGetAddressOf());
	SetDebugName(_srv.Get(), "RenderTarget_SRV");
    if (FAILED(hr)) {
        CCL_LOG_ERROR(LogCategory::Render, "CreateSRV failed: hr=0x%08X", hr);
        Release();
        return false;
    }

    // 4. ビューポート設定
    _viewport.Width = static_cast<float>(width);
    _viewport.Height = static_cast<float>(height);
    _viewport.MinDepth = 0.0f;
    _viewport.MaxDepth = 1.0f;
    _viewport.TopLeftX = 0.0f;
    _viewport.TopLeftY = 0.0f;

    return true;
}

void RenderTarget::Clear(ID3D11DeviceContext* dc, const float color[4])
{
    if (dc && _rtv) {
        dc->ClearRenderTargetView(_rtv.Get(), color);
    }
}

void RenderTarget::Activate(ID3D11DeviceContext* dc, ID3D11DepthStencilView* dsv)
{
    if (!dc || !_rtv) return;
    dc->RSSetViewports(1, &_viewport);
    dc->OMSetRenderTargets(1, _rtv.GetAddressOf(), dsv);
}