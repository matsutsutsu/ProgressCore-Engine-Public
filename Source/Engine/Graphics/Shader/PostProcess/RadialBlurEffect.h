// 放射状ブラー(集中線)ポストエフェクト。
// 役割      : 画面中心へ向かうブラーで「速度感」を出すフルスクリーン効果。
// 責務      : RadialBlurPS を1パス実行し、入力HDR→出力HDRへ書き出す。
// 位置付け  : IPostEffect のチェーンの1段(Bloom より前に置く)。
// 設計方針  : 中間RTを持たず、ping-pong で保証される別テクスチャの
//             inputSRV→outputRTV へ直接描く(DistortionのようなCopyResourceを省略)。
//             パラメータは RadialBlurConfigComponent → PostProcessSystem 経由で流入。
#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

// 放射状ブラー(集中線)を適用するポストエフェクト。
//
// strength<=0 のときは入力をそのまま出力へ流す(パススルー)。
// ゲーム側がダッシュ/被弾等の瞬間に strength を上げて時間で戻す使い方を想定。
class RadialBlurEffect : public IPostEffect
{
public:
    RadialBlurEffect() = default;
    ~RadialBlurEffect() override = default;

    // シェーダー・定数バッファ・サンプラを生成する。中間RTは持たない。
    void Initialize(ID3D11Device* device, UINT width, UINT height) override;

    // リサイズ時の処理。中間RTを持たないため何もしない。
    void Resize(ID3D11Device* device, UINT width, UINT height) override;

    /**
     * @brief 放射状ブラーを適用する。
     * @param inputSRV  前段の出力(シーンHDR)。
     * @param outputRTV 書き出し先(ping-pong により inputSRV とは別テクスチャ)。
     * @note  strength<=0 のときは CopyResource でパススルーする。
     */
    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "RadialBlur"; }

    // ECS(PostProcessSystem)からパラメータを受け取る。
    void UpdateParams(const GpuData::CbRadialBlurData& data) { m_cbData = data; }

private:
    GpuData::CbRadialBlurData m_cbData{};

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
