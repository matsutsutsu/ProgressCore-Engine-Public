#pragma once
#include <d3d11.h>
#include <wrl.h>

// TracyのD3D11用ヘッダー
#include "tracy/TracyD3D11.hpp"

/**
 * @brief D3D11 デバイス・スワップチェーン・RT・定数バッファを所有する RHI 層。
 * @details
 *  Graphics クラスから「D3D11 の配管作業」を分離した。
 *  シングルトンではなく Graphics の値メンバとして保持される。
 *
 *  責務:
 *  - デバイス/コンテキスト/スワップチェーンの生成と所有
 *  - バックバッファ RTV / DSV / ゲームビュー RTV・SRV の管理
 *  - CbScene 定数バッファの保持と GPU 転送
 *  - Clear / SetRenderTargets / Present の実行
 *
 *  責務外:
 *  - 描画サブシステムの管理（ModelConstantBuffers 等）→ Graphics が担う
 *  - シェーダー登録、ライト更新 → Graphics が担う
 */
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

    // ---- Core access ----
    ID3D11Device        *GetDevice() const noexcept { return m_device.Get(); }
    ID3D11DeviceContext *GetContext() const noexcept { return m_context.Get(); }
    IDXGISwapChain      *GetSwapChain() const noexcept { return m_swapChain.Get(); }

    // ---- Render target access ----
    ID3D11RenderTargetView   *GetBackBufferRTV() const noexcept { return m_backBufferRTV.Get(); }
    ID3D11DepthStencilView   *GetDepthStencilView() const noexcept { return m_depthStencilView.Get(); }
    ID3D11ShaderResourceView *GetDepthSRV() const noexcept { return m_depthSRV.Get(); }
    ID3D11RenderTargetView   *GetGameRTV() const noexcept { return m_gameRTV.Get(); }
    ID3D11ShaderResourceView *GetGameSRV() const noexcept { return m_gameSRV.Get(); }

    // Tracyコンテキストへのアクセサ
    // ※ tracy::D3D11Ctx は TRACY_ENABLE 未定義（Shipping）だと存在しないため、
    //    両構成で定義される移植用エイリアス TracyD3D11Ctx（無効時は void*）を使う。
    TracyD3D11Ctx GetTracyContext() const noexcept { return m_tracyD3D11Ctx; }

    // ---- CbScene 定数バッファ ----
    ID3D11Buffer *GetSceneConstantBuffer() const noexcept { return m_sceneConstantBuffer.Get(); }

    /**
     * @brief CbScene バッファの内容を GPU に転送する。
     * @param data  16 バイトアライン済みの struct へのポインタ。
     * @param size  struct のバイト数。
     */
    void UpdateSceneConstantBuffer(const void *data, size_t size);

    // ---- Viewport / screen size ----
    HWND  GetWindowHandle() const noexcept { return m_hWnd; }
    float GetWidth() const noexcept { return static_cast<float>(m_width); }
    float GetHeight() const noexcept { return static_cast<float>(m_height); }
    UINT  GetWidthU() const noexcept { return m_width; }
    UINT  GetHeightU() const noexcept { return m_height; }

    // ---- Frame operations ----
    void    Clear(float r, float g, float b, float a, bool useGameRTV);
    void    SetRenderTargets(bool useGameRTV);
    HRESULT Present(UINT syncInterval);

    /**
     * @brief 次フレームを構築してよくなるまで(前フレームの提示完了まで)ブロックする。
     * @details Waitable スワップチェーンの待機オブジェクトを待つ。フレーム先頭で1回だけ呼ぶ。
     *          これで vsync待ちがこの1箇所に集約され、各描画パスの計測に紛れ込まない。
     *          待機ハンドルが無い(生成失敗)場合は即座に返る。
     */
    void WaitForFrameLatency() const noexcept;

    HRESULT GetDeviceRemovedReason() const noexcept;

private:
    HWND m_hWnd   = nullptr;
    UINT m_width  = 0;
    UINT m_height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGISwapChain>      m_swapChain;

    // Waitable スワップチェーンの待機オブジェクト（フレーム先頭で待つ）。
    //   スワップチェーンが所有する扱いなので明示 CloseHandle はしない（プロセス終了で回収）。
    HANDLE                                           m_frameLatencyWaitable = nullptr;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_backBufferRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_depthSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_gameTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_gameRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_gameSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_sceneConstantBuffer;
    D3D11_VIEWPORT                                   m_viewport{};

    // Tracy D3D11 コンテキストのポインタ（無効構成では TracyD3D11Ctx=void*）
    TracyD3D11Ctx m_tracyD3D11Ctx = nullptr;
};