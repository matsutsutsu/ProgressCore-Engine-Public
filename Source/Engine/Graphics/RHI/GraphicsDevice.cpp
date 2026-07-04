#include "GraphicsDevice.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"  // _ASSERT_EXPR, HRTrace
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Logger.h"

// CbScene 構造体（GraphicsDevice が物理バッファを持ち、Graphics が内容を埋める）
// バッファサイズだけ知れればよいため private static constexpr で宣言

void GraphicsDevice::Initialize(HWND hWnd)
{
    _hWnd = hWnd;

    RECT rc;
    GetClientRect(hWnd, &rc);
    _width = static_cast<UINT>(rc.right - rc.left);
    _height = static_cast<UINT>(rc.bottom - rc.top);

    HRESULT hr = S_OK;

    // ---- デバイス & スワップチェーン ----
    {
        UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,  D3D_FEATURE_LEVEL_9_1,
        };

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = _width;
        desc.BufferDesc.Height = _height;
        desc.BufferDesc.RefreshRate = { 60, 1 };
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = hWnd;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            createDeviceFlags, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION, &desc,
            _swapChain.GetAddressOf(), _device.GetAddressOf(),
            &featureLevel, _context.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- バックバッファ RTV ----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        hr = _swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(tex.GetAddressOf()));
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = _device->CreateRenderTargetView(tex.Get(), nullptr, _backBufferRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- 深度ステンシル ----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = _width; d.Height = _height;
        d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        d.SampleDesc = { 1, 0 };
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = _device->CreateTexture2D(&d, nullptr, tex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = _device->CreateDepthStencilView(tex.Get(), nullptr, _depthStencilView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- ビューポート ----
    _viewport = { 0.f, 0.f,
                  static_cast<float>(_width), static_cast<float>(_height),
                  0.f, 1.f };

    // ---- ゲームビュー用 RTT（エディタ表示 / 最終出力先） ----
    {
        D3D11_TEXTURE2D_DESC t{};
        t.Width = _width; t.Height = _height;
        t.MipLevels = 1; t.ArraySize = 1;
        t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc = { 1, 0 };
        t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        _device->CreateTexture2D(&t, nullptr, _gameTex.GetAddressOf());
        _device->CreateRenderTargetView(_gameTex.Get(), nullptr, _gameRTV.GetAddressOf());
        _device->CreateShaderResourceView(_gameTex.Get(), nullptr, _gameSRV.GetAddressOf());
        SetDebugName(_gameSRV.Get(), "GameView_SRV");
    }

    // ---- CbScene 定数バッファ (b8) ----
    // サイズは ShaderShared.h の GpuData::CbScene と一致（sizeof で自動追従）
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(GpuData::CbScene); // ShaderShared.h で定義
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = _device->CreateBuffer(&bd, nullptr, _sceneConstantBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // デバイスとコンテキストの準備が完了した最後に Tracy コンテキストを生成
    if (_device && _context) {
        _tracyD3D11Ctx = TracyD3D11Context(_device.Get(), _context.Get());
        CCL_LOG_INFO(LogCategory::Render,"[GraphicsDevice] Tracy D3D11 Context initialized.");
        
    }
}

void GraphicsDevice::Shutdown()
{
    // ① コンテキスト状態クリア（サブシステム reset 後に呼ぶこと）
    if (_context) {
        _context->ClearState();
        _context->Flush();
    }

    // デバイス本体が破壊される「前」に Tracy コンテキストを破棄する
    if (_tracyD3D11Ctx) {
        TracyD3D11Destroy(_tracyD3D11Ctx);
        _tracyD3D11Ctx = nullptr;
        CCL_LOG_INFO(LogCategory::Render, "[GraphicsDevice] Tracy D3D11 Context destroyed.");
    }

    // ② GraphicsDevice 自身が持っているリソースを【先に】全て手放す！
    _sceneConstantBuffer.Reset();
    _gameSRV.Reset();
    _gameRTV.Reset();
    _gameTex.Reset();
    _depthStencilView.Reset();
    _backBufferRTV.Reset();
    _swapChain.Reset();
    _context.Reset();

    // ③ この時点でデバイス（_device）に残っているものは「真のリーク（犯人）」だけ！
    // ここでレポートを出力する デバッグ時のみ
#ifdef ENABLE_EDITOR
    if (_device) {
        Microsoft::WRL::ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(_device.As(&debug))) {
            OutputDebugStringA("\n================= D3D11 LEAK REPORT =================\n");
            // これで、出力ウィンドウに「Texture2D」や「Buffer」などの名前がバッチリ出ます
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            OutputDebugStringA("=====================================================\n\n");
        }
    }
#endif

    // ④ 最後にデバイス本体を破壊
    _device.Reset();
}

void GraphicsDevice::UpdateSceneConstantBuffer(const void* data, size_t /*size*/)
{
    if (_sceneConstantBuffer && _context && data) {
        _context->UpdateSubresource(_sceneConstantBuffer.Get(), 0, nullptr, data, 0, 0);
    }
}

void GraphicsDevice::Clear(float r, float g, float b, float a, bool useGameRTV)
{
    float color[4]{ r, g, b, a };
    if (useGameRTV)
        _context->ClearRenderTargetView(_gameRTV.Get(), color);
    else
        _context->ClearRenderTargetView(_backBufferRTV.Get(), color);
    _context->ClearDepthStencilView(_depthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void GraphicsDevice::SetRenderTargets(bool useGameRTV)
{
    if (useGameRTV)
        _context->OMSetRenderTargets(1, _gameRTV.GetAddressOf(), _depthStencilView.Get());
    else
        _context->OMSetRenderTargets(1, _backBufferRTV.GetAddressOf(), _depthStencilView.Get());
    _context->RSSetViewports(1, &_viewport);
}


HRESULT GraphicsDevice::Present(UINT syncInterval)
{
    return _swapChain->Present(syncInterval, 0);
}

/**
 * @brief デバイスが失われた原因コードを返す。
 *        DXGI_ERROR_DEVICE_HUNG / _RESET / _REMOVED / DXGI_ERROR_DRIVER_INTERNAL_ERROR など
 */
HRESULT GraphicsDevice::GetDeviceRemovedReason() const noexcept
{
    return _device ? _device->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
}