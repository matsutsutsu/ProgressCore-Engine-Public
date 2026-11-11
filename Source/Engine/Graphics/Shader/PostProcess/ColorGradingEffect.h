#pragma once
// ===========================================================================
// 概要: 色味・彩度・コントラスト・明るさを変えるカラーグレーディングのエフェクト
//
// 責務:
//   - ColorGradingPS を1回かける。混ぜる量が 0 以下なら入力をそのまま写す
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class ColorGradingEffect : public IPostEffect
{
public:
    ColorGradingEffect()           = default;
    ~ColorGradingEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "ColorGrading"; }
    void        UpdateParams(const GpuData::CbColorGradingData &data) { m_cbData = data; }

private:
    GpuData::CbColorGradingData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
