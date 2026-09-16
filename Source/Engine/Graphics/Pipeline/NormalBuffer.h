// 画面空間のビュー空間法線を保持するレンダーターゲット。
// 役割      : NormalPrepass が書き込み、SSAO/SSR等が t6 で読む法線バッファ。
// 責務      : RTの生成/解放/リサイズ、RTVとSRVの提供。
// 位置付け  : ShadowMap と同じ「専用バッファを持つ描画リソース」。
// 設計方針  : RG16_FLOAT でビュー空間法線の XY のみ保持(Zは読む側で復元)。
//             フル解像度で持つ(SSAOがフル解像度で読むため)。
#pragma once
#include <d3d11.h>
#include <wrl/client.h>

// ビュー空間法線を格納する画面空間バッファ。
class NormalBuffer
{
public:
    /**
     * @brief 指定解像度でRTを生成する。
     * @return 生成に成功したら true。
     */
    bool Create(ID3D11Device *device, UINT width, UINT height);

    // 解像度変更時に作り直す。
    void Resize(ID3D11Device *device, UINT width, UINT height);

    /**
     * @brief RTをクリアし、描画先として設定する。
     * @param dsv シーンの深度バッファ。深度テストを効かせて正しい前後関係で法線を書く。
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
    UINT                                             m_width  = 0;
    UINT                                             m_height = 0;
};
