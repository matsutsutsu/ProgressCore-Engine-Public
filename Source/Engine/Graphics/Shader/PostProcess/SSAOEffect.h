/// @file  SSAOEffect.h
/// @brief SSAO(スクリーンスペース・アンビエントオクルージョン)ポストエフェクト。
///        役割      : 深度から遮蔽を計算し、隙間・接地面・くぼみを暗くして立体感を出す。
///        責務      : SSAOPS を1パス実行し、入力HDR→出力HDRへ書き出す。
///        位置付け  : IPostEffect チェーンの最前段(Bloom より前)。
///        設計方針  : G-Buffer不要。深度(t4)から法線を再構築するため、フェーズ1の
///                    深度基盤だけで動く。中間RTは持たない。
///        依存      : シーン深度基盤(GraphicsDevice の SRV化 + Manager の t4 バインド)。
#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

/// @brief SSAO を適用するポストエフェクト。
/// @details ssaoIntensity<=0 のときは入力をそのまま出力へ流す(パススルー)。
/// @note    フォワード構成の割り切りとして、AOは最終カラーへ乗算する
///          (本来は環境光のみに掛けるのが物理的に正しい)。
class SSAOEffect : public IPostEffect
{
public:
    SSAOEffect() = default;
    ~SSAOEffect() override = default;

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;

    /// @brief SSAOを適用する。t4のシーン深度は Manager がバインド済みである前提。
    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "SSAO"; }

    void UpdateParams(const GpuData::CbSSAOData& data) { _cbData = data; }

private:
    GpuData::CbSSAOData _cbData{};

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> _res;
};
