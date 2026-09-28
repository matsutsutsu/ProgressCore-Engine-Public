#pragma once
// ===========================================================================
// 概要: 画面の各ピクセルのビュー空間の法線を持つレンダーターゲット。
//
// 責務:
//   - テクスチャと RTV・SRV を作る・サイズが変わったら作り直す
//   - クリアして描画先にする（NormalPrepass が使う）
//
// 備考: 形式は RG16_FLOAT で、法線の XY だけを持つ（Z は読む側で求める）。
//       SSAO がフル解像度で読むので、フル解像度で持つ。
// ===========================================================================
#include <d3d11.h>
#include <wrl/client.h>

class NormalBuffer
{
public:
    // width × height（px）で作る。作れたら true
    bool Create(ID3D11Device *device, UINT width, UINT height);

    // サイズが変わったときだけ作り直す
    void Resize(ID3D11Device *device, UINT width, UINT height);

    /**
     * @brief クリアして、描画先とビューポートを設定する。
     * @param dsv シーンの深度バッファ。深度テストをして、手前の面の法線だけを残す
     */
    void Activate(ID3D11DeviceContext *dc, ID3D11DepthStencilView *dsv);

    ID3D11RenderTargetView   *GetRTV() const noexcept { return m_rtv.Get(); }
    ID3D11ShaderResourceView *GetSRV() const noexcept { return m_srv.Get(); }
    UINT                      GetWidth() const noexcept { return m_width; }
    UINT                      GetHeight() const noexcept { return m_height; }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    UINT                                             m_width  = 0; // px
    UINT                                             m_height = 0; // px
};
