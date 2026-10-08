#pragma once
// ===========================================================================
// 概要: ピントの合う距離から離れた所をぼかす被写界深度のエフェクト。
//
// 責務:
//   - 深度（t4）を読み、DepthOfFieldPS を1回かける。ぼかしの最大が 0 以下なら入力をそのまま写す
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class DepthOfFieldEffect : public IPostEffect
{
public:
    DepthOfFieldEffect()           = default;
    ~DepthOfFieldEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "DepthOfField"; }
    void        UpdateParams(const GpuData::CbDepthOfFieldData &data) { m_cbData = data; }

private:
    GpuData::CbDepthOfFieldData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
