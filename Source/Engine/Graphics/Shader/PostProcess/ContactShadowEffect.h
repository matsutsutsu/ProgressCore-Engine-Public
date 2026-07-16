/// @file  ContactShadowEffect.h
/// @brief コンタクトシャドウ(足元/隙間の微小影)ポストエフェクト。
///        役割      : 深度を光源方向へレイマーチし、カスケードシャドウが拾えない
///                    至近距離の接地影を補う。
///        責務      : ContactShadowPS を1パス実行し、入力HDR→出力HDRへ書き出す。
///        位置付け  : IPostEffect チェーンの最前段(SSAOの直後)。
///        設計方針  : G-Buffer不要。深度(t4)のみでレイマーチする。中間RTは持たない。
///        依存      : シーン深度基盤(GraphicsDevice の SRV化 + Manager の t4 バインド)。
#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

/// @brief コンタクトシャドウを適用するポストエフェクト。
/// @details csIntensity<=0 のときは入力をそのまま出力へ流す(パススルー)。
class ContactShadowEffect : public IPostEffect
{
public:
    ContactShadowEffect() = default;
    ~ContactShadowEffect() override = default;

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;

    /// @brief コンタクトシャドウを適用する。t4のシーン深度は Manager がバインド済みである前提。
    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "ContactShadow"; }

    void UpdateParams(const GpuData::CbContactShadowData& data) { _cbData = data; }

private:
    GpuData::CbContactShadowData _cbData{};

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    Microsoft::WRL::ComPtr<ID3D11BlendState> _opaqueBlendState;
    std::unique_ptr<Resources> _res;
};
