#pragma once
// ===========================================================================
// 概要: 中心へ向かってぼかし、速さを感じさせる放射状ブラーのエフェクト
//
// 責務:
//   - RadialBlurPS を1回かけ、入力から出力へ直接描く
//   - 強さが 0 以下なら入力をそのまま写す
//
// 備考: Bloom より前に置く。入力と出力は2枚の HDR バッファの別々の方なので、途中のバッファなしで直接描ける
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class RadialBlurEffect : public IPostEffect
{
public:
    RadialBlurEffect()           = default;
    ~RadialBlurEffect() override = default;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;

    // 途中のバッファを持たないので何もしない
    void Resize(ID3D11Device *device, UINT width, UINT height) override;

    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "RadialBlur"; }

    void UpdateParams(const GpuData::CbRadialBlurData &data) { m_cbData = data; }

private:
    GpuData::CbRadialBlurData m_cbData{};

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
