#include "RenderTarget.h"

// 既存のロガーを使用。CCL_LOG_ERROR / LogCategory がプロジェクト側で定義されている前提。
// 別ロガーを使う場合はここを差し替える。
#include "Engine/Platform/Core/Logger.h"
#include "Engine/Graphics/RHI/DebugName.h"

RenderTarget::~RenderTarget()
{
    Release();
}

void RenderTarget::Release()
{
    m_texture.Reset();
    m_rtv.Reset();
    m_srv.Reset();
    m_width  = 0;
    m_height = 0;
    m_format = DXGI_FORMAT_UNKNOWN;
}

bool RenderTarget::Create(ID3D11Device *device, UINT width, UINT height, DXGI_FORMAT format)
{
    if (!device || width == 0 || height == 0)
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "RenderTarget::Create invalid args (w=%u h=%u)", width, height);
        return false;
    }

    // 既存リソースを確実に解放してから再生成
    Release();
    m_width  = width;
    m_height = height;
    m_format = format;

    // 1. テクスチャ
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width                = width;
    texDesc.Height               = height;
    texDesc.MipLevels            = 1;
    texDesc.ArraySize            = 1;
    texDesc.Format               = format;
    texDesc.SampleDesc.Count     = 1;
    texDesc.Usage                = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags            = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&texDesc, nullptr,
                                         m_texture.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        CCL_LOG_ERROR(LogCategory::Render,
                      "CreateTexture2D failed: hr=0x%08X w=%u h=%u", hr, width, height);
        Release();
        return false;
    }

    // 2. RTV
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format                        = format;
    rtvDesc.ViewDimension                 = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice            = 0;

    hr = device->CreateRenderTargetView(m_texture.Get(), &rtvDesc,
                                        m_rtv.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        CCL_LOG_ERROR(LogCategory::Render, "CreateRTV failed: hr=0x%08X", hr);
        Release();
        return false;
    }

    // 3. SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                          = format;
    srvDesc.ViewDimension                   = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip       = 0;
    srvDesc.Texture2D.MipLevels             = 1;

    hr = device->CreateShaderResourceView(m_texture.Get(), &srvDesc,
                                          m_srv.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        CCL_LOG_ERROR(LogCategory::Render, "CreateSRV failed: hr=0x%08X", hr);
        Release();
        return false;
    }
    // 生成に成功してから名前を付ける（失敗時は m_srv が null で無意味なため）。
    SetDebugName(m_srv.Get(), "RenderTarget_SRV");

    // 4. ビューポート設定
    m_viewport.Width    = static_cast<float>(width);
    m_viewport.Height   = static_cast<float>(height);
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
    m_viewport.TopLeftX = 0.0f;
    m_viewport.TopLeftY = 0.0f;

    return true;
}

void RenderTarget::Clear(ID3D11DeviceContext *dc, const float color[4])
{
    if (dc && m_rtv)
    {
        dc->ClearRenderTargetView(m_rtv.Get(), color);
    }
}

void RenderTarget::Activate(ID3D11DeviceContext *dc, ID3D11DepthStencilView *dsv)
{
    if (!dc || !m_rtv) return;
    dc->RSSetViewports(1, &m_viewport);
    dc->OMSetRenderTargets(1, m_rtv.GetAddressOf(), dsv);
}