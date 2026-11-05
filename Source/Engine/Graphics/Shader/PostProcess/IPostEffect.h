#pragma once
// ===========================================================================
// 概要: ポストプロセスのエフェクト1段分の基底クラス
//
// 責務:
//   - Initialize / Resize / Process の口を用意する
//   - 有効かどうか（m_enabled）を持つ
//
// 備考: PostProcessManager が HDR バッファ2枚を交互に入れ替えながら、有効なエフェクトを登録順に1回ずつ呼ぶ
//         [HDR A] --Process--> [HDR B] --Process--> [HDR A] --> ... --> ToneMap
//       Process は必ず outputRTV の全面を書く。何も書かずに戻ると、次のエフェクトが2フレーム前の絵を読む
//       効果をかけないときも、入力を出力へ CopyResource する。縮小バッファなどは自分で持ち、Resize で作り直す
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

    // エフェクトを1段かける。inputSRV は前の段の出力（最初はシーンの HDR バッファ）
    // outputRTV は、効果をかけないときも含めて必ず全面を書く
    virtual void Process(
        ID3D11DeviceContext      *dc,
        ID3D11ShaderResourceView *inputSRV,
        ID3D11RenderTargetView   *outputRTV) = 0;

    // GPU マーカー（PIX / RenderDoc）に出す名前
    virtual const char *GetName() const = 0;

    bool IsEnabled() const noexcept { return m_enabled; }
    void SetEnabled(bool v) noexcept { m_enabled = v; }

protected:
    // 既定は無効。PostProcessSystem は、シーンに設定のコンポーネントがあるエフェクトだけを有効にする
    // 既定を true にすると、設定のない SSAO や DoF まで毎フレーム走り、GPU の時間を大きく使う
    bool m_enabled = false;
};