#pragma once
#include <d3d11.h>

/**
 * @brief HDR フィルターエフェクトの基底インターフェース。
 *
 * @details
 *  線形チェーン方式: Process(inputSRV) → outputSRV
 *  各エフェクトは自前の中間 RT を ResourceManager で管理する。
 *
 *  実装の雛形:
 *  @code
 *   class VignetteEffect : public IPostEffect {
 *   public:
 *       void Initialize(ID3D11Device* d, UINT w, UINT h) override;
 *       void Resize(ID3D11Device* d, UINT w, UINT h) override;
 *       ID3D11ShaderResourceView* Process(
 *           ID3D11DeviceContext* dc, ID3D11ShaderResourceView* in) override;
 *       const char* GetName() const override { return "Vignette"; }
 *   };
 *
 *   // 登録（Graphics::Initialize 等で）
 *   m_postProcess->AddEffect<VignetteEffect>();
 *  @endcode
 */
class IPostEffect
{
public:
    virtual ~IPostEffect() = default;

    /** デバイス生成時に 1 回呼ぶ。RTの確保もここで行う。 */
    virtual void Initialize(ID3D11Device* device, UINT width, UINT height) = 0;

    /** ウィンドウリサイズ時に呼ぶ。中間 RT の再生成を行う。 */
    virtual void Resize(ID3D11Device* device, UINT width, UINT height) = 0;

    /**
     * @brief エフェクトを適用する。
     * @param inputSRV 前エフェクトの出力（先頭はシーン HDR テクスチャ）
     * @return 処理済み SRV。nullptr を返すと PostProcessManager が inputSRV をそのまま引き継ぐ。
     */
    virtual void Process(
        ID3D11DeviceContext* dc,
        ID3D11ShaderResourceView* inputSRV,
        ID3D11RenderTargetView* outputRTV) = 0;

    /** ImGui 等で表示するエフェクト名 */
    virtual const char* GetName() const = 0;

    bool IsEnabled()     const noexcept { return m_enabled; }
    void SetEnabled(bool v)    noexcept { m_enabled = v; }

protected:
    // ★既定は OFF（opt-in）。PostProcessSystem は「対応する設定コンポーネントが
    //   存在するエフェクトだけ」SetEnabled() を呼ぶため、既定 true にすると設定の
    //   無いエフェクト（SSAO/ContactShadow/DoF/…）が毎フレーム全走行して GPU を
    //   飽和させる。Bloom と同じく「設定があるときだけ有効」に統一する。
    bool m_enabled = false;
};