#include "GraphicsDevice.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include "Engine/Graphics/RHI/DebugName.h"
#include "Engine/Platform/Core/Logger.h"
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")

// ---------------------------------------------------------------------------
// 初期化と終了
// ---------------------------------------------------------------------------

void GraphicsDevice::Initialize(HWND hWnd)
{
    m_hWnd = hWnd;

    RECT rc;
    GetClientRect(hWnd, &rc);
    m_width  = static_cast<UINT>(rc.right - rc.left);
    m_height = static_cast<UINT>(rc.bottom - rc.top);

    HRESULT hr = S_OK;

    // ---- 高性能 GPU（dGPU）を明示して選ぶ ----
    // ハイブリッド GPU の PC でアダプタに nullptr を渡すと内蔵 GPU が選ばれることがあるので、EnumAdapterByGpuPreference で dGPU を選ぶ
    // 対応していない OS や列挙に失敗したときは chosenAdapter が null のままで、既定のアダプタを使う
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

    // ---- デバイス（スワップチェーンは後で waitable 付きで別に作る）----
    // D3D11CreateDeviceAndSwapChain でまとめて作ってはいけない。vsync の待ちを自分で制御する
    // FRAME_LATENCY_WAITABLE_OBJECT は CreateSwapChainForHwnd からしか使えないので、
    // デバイスとスワップチェーンは分けて作る
    {
        UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1,
        };

        // アダプタを指定するときは DriverType を D3D_DRIVER_TYPE_UNKNOWN にする
        // （pAdapter != nullptr と D3D_DRIVER_TYPE_HARDWARE を一緒に指定すると E_INVALIDARG になる）
        const D3D_DRIVER_TYPE driverType = chosenAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            chosenAdapter.Get(), driverType, nullptr,
            createDeviceFlags, featureLevels, ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            m_device.GetAddressOf(), &featureLevel, m_context.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        if (FAILED(hr)) CCL_LOG_ERROR(LogCategory::Render, "[GraphicsDevice] D3D11 デバイスを作れません: hr=0x%08X", hr);

        // ---- このデバイスを作った DXGI ファクトリ（IDXGIFactory2）を取る ----
        Microsoft::WRL::ComPtr<IDXGIFactory2> factory2;
        {
            Microsoft::WRL::ComPtr<IDXGIDevice>  dxgiDevice;
            Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
            hr = m_device.As(&dxgiDevice);
            _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
            hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
            _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
            hr = adapter->GetParent(IID_PPV_ARGS(factory2.GetAddressOf()));
            _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

            // デバイスが実際に使っている GPU をログに書く
            LogAdapterInfo(adapter.Get(), featureLevel);
        }

        // ---- Waitable スワップチェーン ----
        //   FRAME_LATENCY_WAITABLE_OBJECT で、「今のフレームの提示が終わるまで次のフレームを
        //   作り始めない」ための待機オブジェクトを持つスワップチェーンを作る。vsync の待ちが
        //   フレーム先頭の WaitForFrameLatency() 1か所に集まり、ShadowPass など個々のパスの
        //   CPU ゾーンに混ざらなくなる（Tracy が読みやすくなる）。入力遅延も減る
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width       = m_width;
        scd.Height      = m_height;
        scd.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.SampleDesc  = {1, 0};
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scd.Flags       = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;
        hr = factory2->CreateSwapChainForHwnd(
            m_device.Get(), hWnd, &scd, nullptr /*fullscreen desc=windowed*/, nullptr,
            swapChain1.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        if (FAILED(hr)) CCL_LOG_ERROR(LogCategory::Render, "[GraphicsDevice] スワップチェーンを作れません: hr=0x%08X (%ux%u)", hr, m_width, m_height);
        hr = swapChain1.As(&m_swapChain); // IDXGISwapChain1 は IDXGISwapChain の派生
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        // ---- 最大フレーム遅延を 2 にして、待機ハンドルを取る ----
        //   1 にすると CPU と GPU が交互に待つことになり、両方の時間が足し算になる
        //   合計が1フレーム分（16.7ms など）を超えると、vsync で 30FPS に落ちる
        //   2 なら CPU（次のフレーム）と GPU（今のフレーム）が1枚分重なって並行に動き、60FPS を保てる
        //   待ちは WaitForFrameLatency() の1か所のままで、既定の 3 より遅延も小さい
        Microsoft::WRL::ComPtr<IDXGISwapChain2> swapChain2;
        if (SUCCEEDED(m_swapChain.As(&swapChain2)))
        {
            swapChain2->SetMaximumFrameLatency(2);
            m_frameLatencyWaitable = swapChain2->GetFrameLatencyWaitableObject();
        }
    }

    // ---- バックバッファ RTV ----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void **>(tex.GetAddressOf()));
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = m_device->CreateRenderTargetView(tex.Get(), nullptr, m_backBufferRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- 深度ステンシル（SRV でも読めるよう、typeless のテクスチャに DSV と SRV を作る）----
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        D3D11_TEXTURE2D_DESC                    d{};
        d.Width      = m_width;
        d.Height     = m_height;
        d.MipLevels  = 1;
        d.ArraySize  = 1;
        d.Format     = DXGI_FORMAT_R24G8_TYPELESS;
        d.SampleDesc = {1, 0};
        d.Usage      = D3D11_USAGE_DEFAULT;
        d.BindFlags  = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        hr           = m_device->CreateTexture2D(&d, nullptr, tex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        if (FAILED(hr)) CCL_LOG_ERROR(LogCategory::Render, "[GraphicsDevice] 深度バッファを作れません: hr=0x%08X (%ux%u)", hr, m_width, m_height);

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr                    = m_device->CreateDepthStencilView(tex.Get(), &dsvDesc, m_depthStencilView.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format              = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr                          = m_device->CreateShaderResourceView(tex.Get(), &srvDesc, m_depthSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // ---- ビューポート ----
    m_viewport = {0.f, 0.f,
                  static_cast<float>(m_width), static_cast<float>(m_height),
                  0.f, 1.f};

    // ---- ゲームビュー用のレンダーテクスチャ（エディタでの表示・最終出力先）----
    {
        D3D11_TEXTURE2D_DESC t{};
        t.Width      = m_width;
        t.Height     = m_height;
        t.MipLevels  = 1;
        t.ArraySize  = 1;
        t.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc = {1, 0};
        t.Usage      = D3D11_USAGE_DEFAULT;
        t.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        // 他のリソースと同じく HRESULT を確かめる。失敗を見逃すと null の RTV を
        // OMSetRenderTargets し続け、何も映らないのに落ちもしないという、原因を追いにくい壊れ方をする
        hr = m_device->CreateTexture2D(&t, nullptr, m_gameTex.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = m_device->CreateRenderTargetView(m_gameTex.Get(), nullptr, m_gameRTV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        hr = m_device->CreateShaderResourceView(m_gameTex.Get(), nullptr, m_gameSRV.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
        SetDebugName(m_gameSRV.Get(), "GameView_SRV");
    }

    // ---- CbScene 定数バッファ (b8) ----
    // 大きさは ShaderShared.h の GpuData::CbScene の sizeof に合わせる
    {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(GpuData::CbScene);
        bd.Usage     = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr           = m_device->CreateBuffer(&bd, nullptr, m_sceneConstantBuffer.GetAddressOf());
        _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    }

    // デバイスとコンテキストができた後に、Tracy の GPU 計測コンテキストを作る
    if (m_device && m_context)
    {
        m_tracyD3D11Ctx = TracyD3D11Context(m_device.Get(), m_context.Get());
        CCL_LOG_INFO(LogCategory::Render, "[GraphicsDevice] Tracy D3D11 Context initialized.");
    }
}



void GraphicsDevice::Shutdown()
{
    // 1. コンテキストの状態を消す（描画サブシステムを解放した後に呼ぶこと）
    if (m_context)
    {
        m_context->ClearState();
        m_context->Flush();
    }

    // Tracy のコンテキストはデバイスより先に破棄する
    if (m_tracyD3D11Ctx)
    {
        TracyD3D11Destroy(m_tracyD3D11Ctx);
        m_tracyD3D11Ctx = nullptr;
        CCL_LOG_INFO(LogCategory::Render, "[GraphicsDevice] Tracy D3D11 Context destroyed.");
    }

    // 2. GraphicsDevice が持つリソースを先に全部解放する
    m_sceneConstantBuffer.Reset();
    m_gameSRV.Reset();
    m_gameRTV.Reset();
    m_gameTex.Reset();
    m_depthStencilView.Reset();
    m_depthSRV.Reset();
    m_backBufferRTV.Reset();
    m_swapChain.Reset();
    // 待機ハンドルはスワップチェーンと一緒に無効になるので捨てる（無効なハンドルを待たないように）
    // デバイスロストからの復帰では、Initialize が新しいスワップチェーンから取り直す
    m_frameLatencyWaitable = nullptr;
    m_context.Reset();

    // 3. この時点でデバイスに残っているオブジェクトは解放漏れ。エディタ入りのビルドでは一覧を出す
#ifdef ENABLE_EDITOR
    if (m_device)
    {
        Microsoft::WRL::ComPtr<ID3D11Debug> debug;
        if (SUCCEEDED(m_device.As(&debug)))
        {
            OutputDebugStringA("\n================= D3D11 LEAK REPORT =================\n");
            // 出力ウィンドウに、解放されていない Texture2D / Buffer などが型名付きで並ぶ
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
            OutputDebugStringA("=====================================================\n\n");
        }
    }
#endif

    // 4. 最後にデバイスを破棄する
    m_device.Reset();
}



// ---------------------------------------------------------------------------
// 毎フレームの操作
// ---------------------------------------------------------------------------

void GraphicsDevice::UpdateSceneConstantBuffer(const void *data, size_t /*size*/)
{
    if (m_sceneConstantBuffer && m_context && data)
    {
        m_context->UpdateSubresource(m_sceneConstantBuffer.Get(), 0, nullptr, data, 0, 0);
    }
}



void GraphicsDevice::Clear(float r, float g, float b, float a, bool useGameRTV)
{
    float color[4]{r, g, b, a};
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



HRESULT GraphicsDevice::GetDeviceRemovedReason() const noexcept
{
    return m_device ? m_device->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
}



void GraphicsDevice::WaitForFrameLatency() const noexcept
{
    // 待機ハンドルがない（作れなかった）ときは待たずに進む
    if (!m_frameLatencyWaitable) return;

    // 前のフレームの提示が終わり、次を作ってよくなるまで待つ
    //   1秒のタイムアウトはハングしたときのため（普段は vsync の間隔でシグナルされる）
    WaitForSingleObjectEx(m_frameLatencyWaitable, 1000, TRUE);
}



// ---------------------------------------------------------------------------
// 実行環境の報告
// ---------------------------------------------------------------------------

void GraphicsDevice::LogAdapterInfo(IDXGIAdapter *adapter, D3D_FEATURE_LEVEL featureLevel) const
{
    // --- GPU とドライバ ---
    DXGI_ADAPTER_DESC desc{};
    if (adapter && SUCCEEDED(adapter->GetDesc(&desc)))
    {
        // ドライバの版は 64bit の値に 16bit ずつ4つ入っている（例: 31.0.15.5222）
        LARGE_INTEGER driver{};
        const bool    hasDriver = SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver));

        CCL_LOG_ALWAYS(LogCategory::Render, "[環境] GPU: %ls / VRAM %zu MB",
                       desc.Description, desc.DedicatedVideoMemory / (1024 * 1024));
        if (hasDriver)
        {
            CCL_LOG_ALWAYS(LogCategory::Render, "[環境] ドライバ: %u.%u.%u.%u",
                           HIWORD(driver.HighPart), LOWORD(driver.HighPart), HIWORD(driver.LowPart), LOWORD(driver.LowPart));
        }
    }

    // --- 機能レベル ---
    // シェーダーはすべて SM5.0 で作ってあるので、11_0 より下のデバイスでは1つも作れない
    const unsigned major = (static_cast<unsigned>(featureLevel) >> 12) & 0xF;
    const unsigned minor = (static_cast<unsigned>(featureLevel) >> 8) & 0xF;
    CCL_LOG_ALWAYS(LogCategory::Render, "[環境] 機能レベル: %u_%u", major, minor);
    if (featureLevel < D3D_FEATURE_LEVEL_11_0)
    {
        CCL_LOG_ERROR(LogCategory::Render, "[GraphicsDevice] 機能レベル %u_%u では SM5.0 のシェーダーを作れません（11_0 以上が必要）", major, minor);
    }

    // --- 画面 ---
    // 描画の大きさと、モニタの解像度・リフレッシュレート
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode);
    CCL_LOG_ALWAYS(LogCategory::Render, "[環境] 描画 %ux%u / モニタ %lux%lu %luHz / リモートデスクトップ: %s",
                   m_width, m_height, mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency,
                   GetSystemMetrics(SM_REMOTESESSION) ? "はい" : "いいえ");
}