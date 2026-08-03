#pragma once
#include <d3d11.h>
#include <wrl/client.h>

/**
 * @brief レンダーターゲット（テクスチャ + RTV + SRV）の所有者。
 * @details
 *  オフスクリーン描画やポストエフェクトの中継バッファに使用。
 *  以前は全実装をヘッダに置いていたが、共有ヘッダのためビルド速度を圧迫していた。
 *  実装を .cpp に分離してインクルードコストを下げる。
 *
 *  ライフサイクル：
 *  - Create() でリソース生成
 *  - Release() または デストラクタで解放
 *  - 再 Create() で安全に作り直し可能
 */
class RenderTarget
{
public:
    RenderTarget() = default;
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    /**
     * @brief GPU リソースを生成する。既存があれば破棄して作り直す。
     * @return 成功時 true。失敗時はログ出力後 false（既存リソースは解放済）。
     */
    bool Create(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format);

    /**
     * @brief 保持リソースを明示的に解放する。
     */
    void Release();

    /**
     * @brief 有効なリソースを保持しているか。
     */
    bool IsValid() const noexcept { return m_rtv != nullptr; }

    /**
     * @brief このレンダーターゲットを指定色でクリアする。
     */
    void Clear(ID3D11DeviceContext* dc, const float color[4]);

    /**
     * @brief 出力先として設定する（ビューポートも自動セット）。
     * @param dsv 深度ステンシルビュー。不要なら nullptr。
     */
    void Activate(ID3D11DeviceContext* dc, ID3D11DepthStencilView* dsv = nullptr);

    // ---- アクセサ ----
    ID3D11ShaderResourceView* GetSRV()    const noexcept { return m_srv.Get(); }
    ID3D11RenderTargetView* GetRTV()    const noexcept { return m_rtv.Get(); }
    UINT                      GetWidth()  const noexcept { return m_width; }
    UINT                      GetHeight() const noexcept { return m_height; }
    DXGI_FORMAT               GetFormat() const noexcept { return m_format; }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_texture;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   m_rtv;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    D3D11_VIEWPORT m_viewport = {};
    UINT           m_width = 0;
    UINT           m_height = 0;
    DXGI_FORMAT    m_format = DXGI_FORMAT_UNKNOWN;
};