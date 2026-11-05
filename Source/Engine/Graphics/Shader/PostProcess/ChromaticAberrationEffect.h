#pragma once
// ===========================================================================
// 概要: 中心から外へ向かって RGB をずらす色収差のエフェクト
//
// 責務:
//   - ChromaticAberrationPS を1回かける。強さが 0 以下なら入力をそのまま写す
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class ChromaticAberrationEffect : public IPostEffect
{
public:
    ChromaticAberrationEffect()           = default;
    ~ChromaticAberrationEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "ChromaticAberration"; }
    void        UpdateParams(const GpuData::CbChromaticData &data) { m_cbData = data; }

private:
    GpuData::CbChromaticData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
