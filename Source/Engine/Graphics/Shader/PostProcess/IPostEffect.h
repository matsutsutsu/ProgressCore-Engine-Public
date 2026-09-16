#pragma once
#include <d3d11.h>

/**
 * @brief ポストプロセス 1 段ぶんの処理を表すインターフェース。
 *
 * @details
 *  PostProcessManager が HDR バッファ 2 枚を ping-pong させながら、
 *  有効なエフェクトを登録順に 1 回ずつ呼び出す。
 *
 *      [HDR A] --Process--> [HDR B] --Process--> [HDR A] --> ... --> ToneMap
 *
 *  ■ 実装時の契約（重要）
 *    Process は outputRTV の全面を必ず書くこと。
 *    Manager は Process の直後に読み書きバッファを無条件で入れ替えるため、
 *    何も書かずに return すると次のエフェクトが 2 フレーム前の残骸を読む。
 *    「今回は処理しない」場合も input を output へ CopyResource して素通しする
 *    （VignetteEffect::Process 冒頭が実例）。
 *
 *  ■ 有効/無効
 *    既定は OFF。PostProcessSystem が対応する設定コンポーネントを見つけた
 *    ときだけ SetEnabled(true) される（m_enabled のコメントを参照）。
 *
 *  ■ 中間バッファ
 *    縮小バッファ等が要るエフェクトは自前で持つ（BloomEffect が該当）。
 *    その場合は Resize で作り直すこと。
 *
 *  実装の雛形:
 *  @code
 *   class VignetteEffect : public IPostEffect {
 *   public:
 *       void Initialize(ID3D11Device* d, UINT w, UINT h) override;
 *       void Resize(ID3D11Device* d, UINT w, UINT h) override;
 *       void Process(ID3D11DeviceContext* dc,
 *                    ID3D11ShaderResourceView* inputSRV,
 *                    ID3D11RenderTargetView* outputRTV) override;
 *       const char* GetName() const override { return "Vignette"; }
 *   };
 *  @endcode
 */
class IPostEffect
{
public:
    virtual ~IPostEffect() = default;

    /** デバイス生成時に 1 回呼ぶ。RTの確保もここで行う。 */
    virtual void Initialize(ID3D11Device *device, UINT width, UINT height) = 0;

    /** ウィンドウリサイズ時に呼ぶ。中間 RT の再生成を行う。 */
    virtual void Resize(ID3D11Device *device, UINT width, UINT height) = 0;

    /**
     * @brief エフェクトを 1 段適用する。
     * @param dc        デバイスコンテキスト。
     * @param inputSRV  前段の出力（先頭段はシーンを描いた HDR バッファ）。
     * @param outputRTV 書き込み先。素通しの場合も含め、必ず全面を書くこと。
     */
    virtual void Process(
        ID3D11DeviceContext      *dc,
        ID3D11ShaderResourceView *inputSRV,
        ID3D11RenderTargetView   *outputRTV) = 0;

    /** ImGui 等で表示するエフェクト名 */
    virtual const char *GetName() const = 0;

    bool IsEnabled() const noexcept { return m_enabled; }
    void SetEnabled(bool v) noexcept { m_enabled = v; }

protected:
    // 既定は OFF（opt-in）。PostProcessSystem は「対応する設定コンポーネントが
    //   存在するエフェクトだけ」SetEnabled() を呼ぶため、既定 true にすると設定の
    //   無いエフェクト（SSAO/ContactShadow/DoF/…）が毎フレーム全走行して GPU を
    //   飽和させる。Bloom と同じく「設定があるときだけ有効」に統一する。
    bool m_enabled = false;
};