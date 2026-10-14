#pragma once
// ===========================================================================
// 概要: ポストプロセスのエフェクト1段分の基底クラス。
//
// 責務:
//   - Initialize / Resize / Process の口を用意する
//   - 有効かどうか（m_enabled）を持つ
//
// 備考: PostProcessManager が HDR バッファ2枚の読み書きを交互に入れ替えながら、
//       有効なエフェクトを登録した順に1回ずつ呼ぶ。
//         [HDR A] --Process--> [HDR B] --Process--> [HDR A] --> ... --> ToneMap
//
//       Process は必ず outputRTV の全面を書くこと。Manager は Process の後で必ず読み書きを
//       入れ替えるので、何も書かずに戻ると、次のエフェクトは2フレーム前の絵を読む。
//       効果をかけないときも、入力を出力へ CopyResource する（VignetteEffect::Process の最初）。
//
//       縮小バッファなどが要るエフェクトは自分で持ち、Resize で作り直す（BloomEffect など）。
// @code
//    class VignetteEffect : public IPostEffect
//    {
//    public:
//        void Initialize(ID3D11Device *d, UINT w, UINT h) override;
//        void Resize(ID3D11Device *d, UINT w, UINT h) override;
//        void Process(ID3D11DeviceContext *dc,
//                     ID3D11ShaderResourceView *inputSRV,
//                     ID3D11RenderTargetView *outputRTV) override;
//        const char *GetName() const override { return "Vignette"; }
//    };
// @endcode
// ===========================================================================
#include <d3d11.h>

class IPostEffect
{
public:
    virtual ~IPostEffect() = default;

    // PostProcessManager::AddEffect から1回だけ呼ばれる。途中のレンダーターゲットもここで作る
    virtual void Initialize(ID3D11Device *device, UINT width, UINT height) = 0;

    // 画面の大きさ（px）が変わったとき、途中のレンダーターゲットを作り直す
    virtual void Resize(ID3D11Device *device, UINT width, UINT height) = 0;

    /**
     * @brief エフェクトを1段かける。
     * @param inputSRV 前のエフェクトの出力（最初はシーンを描いた HDR バッファ）
     * @param outputRTV 書き込み先。効果をかけないときも含め、必ず全面を書く
     */
    virtual void Process(
        ID3D11DeviceContext      *dc,
        ID3D11ShaderResourceView *inputSRV,
        ID3D11RenderTargetView   *outputRTV) = 0;

    // GPU マーカー（PIX / RenderDoc）に出す名前
    virtual const char *GetName() const = 0;

    bool IsEnabled() const noexcept { return m_enabled; }
    void SetEnabled(bool v) noexcept { m_enabled = v; }

protected:
    // 既定は無効。PostProcessSystem は、シーンに設定のコンポーネントがあるエフェクトだけを
    // 有効にする。既定を true にすると、設定の無いエフェクト（SSAO・DoF など）も
    // 毎フレーム全部走り、GPU の時間を大きく使う
    bool m_enabled = false;
};