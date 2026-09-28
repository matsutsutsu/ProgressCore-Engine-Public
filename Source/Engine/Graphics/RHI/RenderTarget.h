#pragma once
// ===========================================================================
// 概要: テクスチャ・RTV・SRV を1組にしたレンダーターゲット。
//
// 責務:
//   - 指定サイズ・フォーマットでテクスチャと RTV・SRV を作り、持つ
//   - クリアと、出力先への設定（ビューポート込み）を行う
//
// 備考: オフスクリーン描画やポストエフェクトの中間バッファに使う。
//       Create() で作り、Release() かデストラクタで解放する。Create() し直せば作り直せる。
//       多くのファイルが include するので、実装は .cpp に置く（インラインにするとビルドが遅くなる）。
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>

class RenderTarget
{
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget &)            = delete;
    RenderTarget &operator=(const RenderTarget &) = delete;

    /**
     * @brief GPU リソースを作る。既にあれば解放してから作り直す。
     * @return 成功なら true。失敗したらログを出して false（前のリソースは解放済み）。
     */
    bool Create(ID3D11Device *device, UINT width, UINT height, DXGI_FORMAT format);

    // 持っているリソースを解放する
    void Release();

    // リソースを持っているか
    bool IsValid() const noexcept { return m_rtv != nullptr; }

    // 指定色でクリアする
    void Clear(ID3D11DeviceContext *dc, const float color[4]);

    /**
     * @brief 出力先に設定する（ビューポートも合わせて設定する）。
     * @param dsv 深度ステンシルビュー。使わないなら nullptr
     */
    void Activate(ID3D11DeviceContext *dc, ID3D11DepthStencilView *dsv = nullptr);

    // ---- アクセサ ----
    ID3D11ShaderResourceView *GetSRV() const noexcept { return m_srv.Get(); }
    ID3D11RenderTargetView   *GetRTV() const noexcept { return m_rtv.Get(); }
    UINT                      GetWidth() const noexcept { return m_width; }
    UINT                      GetHeight() const noexcept { return m_height; }
    DXGI_FORMAT               GetFormat() const noexcept { return m_format; }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    D3D11_VIEWPORT                                   m_viewport = {};
    UINT                                             m_width    = 0;
    UINT                                             m_height   = 0;
    DXGI_FORMAT                                      m_format   = DXGI_FORMAT_UNKNOWN;
};