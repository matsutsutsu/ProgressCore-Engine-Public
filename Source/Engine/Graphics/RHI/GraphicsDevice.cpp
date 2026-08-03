#include "GraphicsDevice.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"  // _ASSERT_EXPR, HRTrace
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Platform/Core/Logger.h"
#include <dxgi1_6.h> // IDXGIFactory6::EnumAdapterByGpuPreference (高性能GPUの明示選択)
#pragma comment(lib, "dxgi.lib")

// CbScene 構造体（GraphicsDevice が物理バッファを持ち、Graphics が内容を埋める）
// バッファサイズだけ知れればよいため private static constexpr で宣言

void GraphicsDevice::Initialize(HWND hWnd)
{
    m_hWnd = hWnd;

    RECT rc;
    GetClientRect(hWnd, &rc);
    m_width = static_cast<UINT>(rc.right - rc.left);
    m_height = static_cast<UINT>(rc.bottom - rc.top);

    HRESULT hr = S_OK;

    // ---- 高性能GPU(dGPU)の明示選択 ----
    // ハイブリッドグラフィックス(Optimus/PowerXpress)搭載PCでは、アダプタに
    // nullptr を渡すとOS/ドライバの省電力ヒューリスティックで内蔵GPU(Intel等)が
    // 選ばれてしまうことがある。IDXGIFactory6::EnumAdapterByGpuPreference で
    // DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE を明示指定し、確実に dGPU を掴む。
    // 非対応OS(Windows 10 1803未満)やアダプタ列挙に失敗した場合は
    // chosenAdapter が null のままとなり、従来通りの既定アダプタにフォールバックする。
    Microsoft::WRL::ComPtr<IDXGIAdapter1> chosenAdapter;
    {
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory1;
        if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(factory1.GetAddressOf()))))
        {
            Microsoft::WRL::ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(factory1.As(&factory6)))
            {
                factory6->EnumAdapterByGpuPreference(
                    0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(chosenAdapter.GetAddressOf()));
            }
        }

        if (chosenAdapter)
        {
            DXGI_ADAPTER_DESC1 adapterDesc{};
            chosenAdapter->GetDesc1(&adapterDesc);
            CCL_LOG_INFO(LogCategory::Render,
                "[GraphicsDevice] High-performance GPU selected: %ls", adapterDesc.Description);
        }
        else
        {
            CCL_LOG_INFO(LogCategory::Render,
                "[GraphicsDevice] IDXGIFactory6 unavailable; falling back to default adapter selection.");
        }
    }

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
        desc.BufferDesc.Width = m_width;
        desc.BufferDesc.Height = m_height;
        desc.BufferDesc.RefreshRate = { 60, 1 };
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = { 1, 0 };
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = hWnd;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        // アダプタを明示指定する場合、DriverType は D3D_DRIVER_TYPE_UNKNOWN でなければならない
        // (pAdapter != nullptr と D3D_DRIVER_TYPE_HARDWARE の同時指定は E_INVALIDARG になる)。
        const D3D_DRIVER_TYPE driverType = chosenAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDeviceAndSwapChain(
            chosenAdapter.Get(), driverType, nullptr,
            createDeviceFlags, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION, &desc,
            m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
            &featureLevel, m_context.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- バックバッファ RTV ----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(tex.GetAddressOf()));
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = m_device->CreateRenderTargetView(tex.Get(), nullptr, m_backBufferRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- 深度ステンシル (SRV対応: typeless テクスチャ + DSV + SRV) ----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = m_width; d.Height = m_height;
        d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R24G8_TYPELESS;
        d.SampleDesc = { 1, 0 };
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        hr = m_device->CreateTexture2D(&d, nullptr, tex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = m_device->CreateDepthStencilView(tex.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(tex.Get(), &srvDesc, m_depthSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- ビューポート ----
    m_viewport = { 0.f, 0.f,
                  static_cast<float>(m_width), static_cast<float>(m_height),
                  0.f, 1.f };

    // ---- ゲームビュー用 RTT（エディタ表示 / 最終出力先） ----
    {
        D3D11_TEXTURE2D_DESC t{};
        t.Width = m_width; t.Height = m_height;
        t.MipLevels = 1; t.ArraySize = 1;
        t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc = { 1, 0 };
        t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        m_device->CreateTexture2D(&t, nullptr, m_gameTex.GetAddressOf());
        m_device->CreateRenderTargetView(m_gameTex.Get(), nullptr, m_gameRTV.GetAddressOf());
        m_device->CreateShaderResourceView(m_gameTex.Get(), nullptr, m_gameSRV.GetAddressOf());
        SetDebugName(m_gameSRV.Get(), "GameView_SRV");
    }

    // ---- CbScene 定数バッファ (b8) ----
    // サイズは ShaderShared.h の GpuData::CbScene と一致（sizeof で自動追従）
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(GpuData::CbScene); // ShaderShared.h で定義
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = m_device->CreateBuffer(&bd, nullptr, m_sceneConstantBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // デバイスとコンテキストの準備が完了した最後に Tracy コンテキストを生成
    if (m_device && m_context) {
        m_tracyD3D11Ctx = TracyD3D11Context(m_device.Get(), m_context.Get());
        CCL_LOG_INFO(LogCategory::Render,"[GraphicsDevice] Tracy D3D11 Context initialized.");
        
    }
}

void GraphicsDevice::Shutdown()
{
    // ① コンテキスト状態クリア（サブシステム reset 後に呼ぶこと）
    if (m_context) {
        m_context->ClearState();
        m_context->Flush();
    }

    // デバイス本体が破壊される「前」に Tracy コンテキストを破棄する
    if (m_tracyD3D11Ctx) {
        TracyD3D11Destroy(m_tracyD3D11Ctx);
        m_tracyD3D11Ctx = nullptr;
        CCL_LOG_INFO(LogCategory::Render, "[GraphicsDevice] Tracy D3D11 Context destroyed.");
    }

    // ② GraphicsDevice 自身が持っているリソースを【先に】全て手放す！
    m_sceneConstantBuffer.Reset();
    m_gameSRV.Reset();
    m_gameRTV.Reset();
    m_gameTex.Reset();
    m_depthStencilView.Reset();
    m_depthSRV.Reset();
    m_backBufferRTV.Reset();
    m_swapChain.Reset();
    m_context.Reset();

    // ③ この時点でデバイス（m_device）に残っているものは「真のリーク（犯人）」だけ！
    // ここでレポートを出力する デバッグ時のみ
#ifdef ENABLE_EDITOR
    if (m_device) {
        Microsoft::WRL::ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug))) {
            OutputDebugStringA("\n================= D3D11 LEAK REPORT =================\n");
            // これで、出力ウィンドウに「Texture2D」や「Buffer」などの名前がバッチリ出ます
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            OutputDebugStringA("=====================================================\n\n");
        }
    }
#endif

    // ④ 最後にデバイス本体を破壊
    m_device.Reset();
}

void GraphicsDevice::UpdateSceneConstantBuffer(const void* data, size_t /*size*/)
{
    if (m_sceneConstantBuffer && m_context && data) {
        m_context->UpdateSubresource(m_sceneConstantBuffer.Get(), 0, nullptr, data, 0, 0);
    }
}

void GraphicsDevice::Clear(float r, float g, float b, float a, bool useGameRTV)
{
    float color[4]{ r, g, b, a };
    if (useGameRTV)
        m_context->ClearRenderTargetView(m_gameRTV.Get(), color);
    else
        m_context->ClearRenderTargetView(m_backBufferRTV.Get(), color);
    m_context->ClearDepthStencilView(m_depthStencilView.Get(),
        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void GraphicsDevice::SetRenderTargets(bool useGameRTV)
{
    if (useGameRTV)
        m_context->OMSetRenderTargets(1, m_gameRTV.GetAddressOf(), m_depthStencilView.Get());
    else
        m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthStencilView.Get());
    m_context->RSSetViewports(1, &m_viewport);
}


HRESULT GraphicsDevice::Present(UINT syncInterval)
{
    return m_swapChain->Present(syncInterval, 0);
}

/**
 * @brief デバイスが失われた原因コードを返す。
 *        DXGI_ERROR_DEVICE_HUNG / _RESET / _REMOVED / DXGI_ERROR_DRIVER_INTERNAL_ERROR など
 */
HRESULT GraphicsDevice::GetDeviceRemovedReason() const noexcept
{
    return m_device ? m_device->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
}