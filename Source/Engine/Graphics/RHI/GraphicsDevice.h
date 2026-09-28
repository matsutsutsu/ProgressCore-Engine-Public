#pragma once
// ===========================================================================
// 概要: D3D11 のデバイス・スワップチェーン・レンダーターゲット・CbScene を持つ RHI 層。
//
// 責務:
//   - デバイス・コンテキスト・スワップチェーンを作って持つ
//   - バックバッファの RTV、深度（DSV・SRV）、ゲームビュー用の RTV・SRV を持つ
//   - CbScene 定数バッファを持ち、中身を GPU へ送る
//   - Clear / SetRenderTargets / Present を行う
//
// 備考: シングルトンではなく、Graphics が値メンバとして1つ持つ。
//       描画サブシステム（ModelConstantBuffers など）とシェーダー登録は Graphics が受け持つ。
// ===========================================================================
#include <d3d11.h>
#include <wrl.h>

#include "tracy/TracyD3D11.hpp"

class GraphicsDevice
{
public:
    GraphicsDevice()  = default;
    ~GraphicsDevice() = default;

    GraphicsDevice(const GraphicsDevice &)            = delete;
    GraphicsDevice &operator=(const GraphicsDevice &) = delete;

    /**
     * @brief D3D11 デバイス・スワップチェーン・全 RT を生成する。
     * @param hWnd 出力先ウィンドウ。
     */
    void Initialize(HWND hWnd);

    /**
     * @brief 全リソースを解放する。Graphics::Shutdown から呼ぶこと。
     * @details ClearState / Flush / デバッグレポート / ComPtr リセットを行う。
     *          この前にサブシステム（ModelConstantBuffers 等）を reset しておくこと。
     */
    void Shutdown();

    // ---- デバイス ----
    ID3D11Device        *GetDevice() const noexcept { return m_device.Get(); }
    ID3D11DeviceContext *GetContext() const noexcept { return m_context.Get(); }
    IDXGISwapChain      *GetSwapChain() const noexcept { return m_swapChain.Get(); }

    // ---- レンダーターゲット ----
    ID3D11RenderTargetView   *GetBackBufferRTV() const noexcept { return m_backBufferRTV.Get(); }
    ID3D11DepthStencilView   *GetDepthStencilView() const noexcept { return m_depthStencilView.Get(); }
    ID3D11ShaderResourceView *GetDepthSRV() const noexcept { return m_depthSRV.Get(); }
    ID3D11RenderTargetView   *GetGameRTV() const noexcept { return m_gameRTV.Get(); }
    ID3D11ShaderResourceView *GetGameSRV() const noexcept { return m_gameSRV.Get(); }

    // Tracy の GPU 計測コンテキスト。
    // tracy::D3D11Ctx は TRACY_ENABLE が無い構成（Shipping）には無いので、
    // どちらの構成にもある別名 TracyD3D11Ctx（無効時は void*）を使う。
    TracyD3D11Ctx GetTracyContext() const noexcept { return m_tracyD3D11Ctx; }

    // ---- CbScene 定数バッファ ----
    ID3D11Buffer *GetSceneConstantBuffer() const noexcept { return m_sceneConstantBuffer.Get(); }

    /**
     * @brief CbScene の中身を GPU へ送る。
     * @param data GpuData::CbScene へのポインタ
     * @param size 構造体のバイト数（UpdateSubresource はバッファ全体を書くので使っていない）
     */
    void UpdateSceneConstantBuffer(const void *data, size_t size);

    // ---- 画面サイズ ----
    HWND  GetWindowHandle() const noexcept { return m_hWnd; }
    float GetWidth() const noexcept { return static_cast<float>(m_width); }
    float GetHeight() const noexcept { return static_cast<float>(m_height); }
    UINT  GetWidthU() const noexcept { return m_width; }
    UINT  GetHeightU() const noexcept { return m_height; }

    // ---- フレーム操作 ----
    // useGameRTV が true ならゲームビューの RTV、false ならバックバッファを使う
    void    Clear(float r, float g, float b, float a, bool useGameRTV);
    void    SetRenderTargets(bool useGameRTV);
    HRESULT Present(UINT syncInterval);

    /**
     * @brief 前のフレームの提示が終わり、次のフレームを作ってよくなるまで待つ。
     *
     * Waitable スワップチェーンの待機オブジェクトを待つ。フレームの先頭で1回だけ呼ぶ。
     * vsync の待ちがここ1か所に集まるので、各描画パスの計測に混ざらない。
     *
     * @note 待機ハンドルが無い（作れなかった）ときはすぐに返る。
     */
    void WaitForFrameLatency() const noexcept;

    // デバイスが失われた原因（DXGI_ERROR_DEVICE_HUNG / _RESET / _REMOVED / DXGI_ERROR_DRIVER_INTERNAL_ERROR など）
    HRESULT GetDeviceRemovedReason() const noexcept;

private:
    HWND m_hWnd   = nullptr; // 出力先ウィンドウ（所有しない）
    UINT m_width  = 0;       // クライアント領域の幅（px）
    UINT m_height = 0;       // クライアント領域の高さ（px）

    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain>      m_swapChain;

    // Waitable スワップチェーンの待機オブジェクト（フレームの先頭で待つ）。
    // スワップチェーンが持つものとして扱い、CloseHandle は呼ばない（プロセス終了で回収される）。
    HANDLE                                           m_frameLatencyWaitable = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_backBufferRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_depthSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_gameTex;             // ゲームビュー（エディタ表示・最終出力先）
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_gameRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_gameSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_sceneConstantBuffer; // CbScene（b8）。中身は Graphics::Update が書く
    D3D11_VIEWPORT                                   m_viewport{};

    TracyD3D11Ctx m_tracyD3D11Ctx = nullptr; // Tracy の GPU 計測コンテキスト（無効構成では void*）
};