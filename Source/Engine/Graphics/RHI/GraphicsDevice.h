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
 *  - 描画サブシステムの管理（ModelRenderer 等）→ Graphics が担う
 *  - シェーダー登録、ライト更新 → Graphics が担う
 */
class GraphicsDevice
{
public:
    GraphicsDevice() = default;
    ~GraphicsDevice() = default;

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    /**
     * @brief D3D11 デバイス・スワップチェーン・全 RT を生成する。
     * @param hWnd 出力先ウィンドウ。
     */
    void Initialize(HWND hWnd);

    /**
     * @brief 全リソースを解放する。Graphics::Shutdown から呼ぶこと。
     * @details ClearState / Flush / デバッグレポート / ComPtr リセットを行う。
     *          この前にサブシステム（ModelRenderer 等）を reset しておくこと。
     */
    void Shutdown();

    // ---- Core access ----
    ID3D11Device* GetDevice()   const noexcept { return _device.Get(); }
    ID3D11DeviceContext* GetContext()  const noexcept { return _context.Get(); }
    IDXGISwapChain* GetSwapChain() const noexcept { return _swapChain.Get(); }

    // ---- Render target access ----
    ID3D11RenderTargetView* GetBackBufferRTV()    const noexcept { return _backBufferRTV.Get(); }
    ID3D11DepthStencilView* GetDepthStencilView() const noexcept { return _depthStencilView.Get(); }
    ID3D11ShaderResourceView* GetDepthSRV()       const noexcept { return _depthSRV.Get(); }
    ID3D11RenderTargetView* GetGameRTV()          const noexcept { return _gameRTV.Get(); }
    ID3D11ShaderResourceView* GetGameSRV()          const noexcept { return _gameSRV.Get(); }

    // Tracyコンテキストへのアクセサ
    // ※ tracy::D3D11Ctx は TRACY_ENABLE 未定義（Shipping）だと存在しないため、
    //    両構成で定義される移植用エイリアス TracyD3D11Ctx（無効時は void*）を使う。
    TracyD3D11Ctx GetTracyContext() const noexcept { return _tracyD3D11Ctx; }

    // ---- CbScene 定数バッファ ----
    ID3D11Buffer* GetSceneConstantBuffer() const noexcept { return _sceneConstantBuffer.Get(); }

    /**
     * @brief CbScene バッファの内容を GPU に転送する。
     * @param data  16 バイトアライン済みの struct へのポインタ。
     * @param size  struct のバイト数。
     */
    void UpdateSceneConstantBuffer(const void* data, size_t size);

    // ---- Viewport / screen size ----
    HWND  GetWindowHandle() const noexcept { return _hWnd; }
    float GetWidth()        const noexcept { return static_cast<float>(_width); }
    float GetHeight()       const noexcept { return static_cast<float>(_height); }
    UINT  GetWidthU()       const noexcept { return _width; }
    UINT  GetHeightU()      const noexcept { return _height; }

    // ---- Frame operations ----
    void Clear(float r, float g, float b, float a, bool useGameRTV);
    void SetRenderTargets(bool useGameRTV);
    HRESULT Present(UINT syncInterval);

    HRESULT GetDeviceRemovedReason() const noexcept;

private:
    HWND _hWnd = nullptr;
    UINT _width = 0;
    UINT _height = 0;

    Microsoft::WRL::ComPtr<ID3D11Device>             _device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext>      _context;
    Microsoft::WRL::ComPtr<IDXGISwapChain>           _swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   _backBufferRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   _depthStencilView;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _depthSRV;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          _gameTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   _gameRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _gameSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             _sceneConstantBuffer;
    D3D11_VIEWPORT                                   _viewport{};

    // Tracy D3D11 コンテキストのポインタ（無効構成では TracyD3D11Ctx=void*）
    TracyD3D11Ctx _tracyD3D11Ctx = nullptr;
};