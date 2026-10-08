#pragma once
// ===========================================================================
// 概要: 画面全体を時間で揺らす歪みのエフェクト。
//
// 責務:
//   - DistortionPS で途中のバッファに描き、それを出力へ写す
//   - 強さが 0 以下なら入力をそのまま写す
// ===========================================================================
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "IPostEffect.h"
#include <d3d11.h>
#include <memory>
#include <wrl/client.h>

class DistortionEffect : public IPostEffect
{
public:
    DistortionEffect() = default;
    ~DistortionEffect() override;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;

    void Process(ID3D11DeviceContext *dc, ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView *outputRTV) override;

    const char *GetName() const override { return "Distortion"; }

    void UpdateParams(const GpuData::CbDistortionData &data) { m_cbData = data; }

private:
    GpuData::CbDistortionData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbDistortion;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };

    std::unique_ptr<Resources> m_res;
    RenderTargetHandle         m_intermediateRT; // 歪ませた絵を描く途中のバッファ（HDR）
};