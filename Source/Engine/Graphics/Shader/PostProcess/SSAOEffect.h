#pragma once
// ===========================================================================
// 概要: 深度と法線から周りの物に遮られる度合いを求め、隙間やくぼみを暗くする SSAO のエフェクト
//
// 責務:
//   - 遮られる度合いを求める → 深度を見ながら横・縦にぼかす → シーンの色に掛ける の4回で描く
//   - 途中のバッファ（遮られる度合い・横ぼかし・縦ぼかし）を持ち、Resize で作り直す
//
// 備考: エフェクトの列の最初に置く。求めたばかりの値はノイズが多いので、ぼかしてから掛ける
// ===========================================================================
#include "IPostEffect.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class SSAOEffect : public IPostEffect
{
public:
    SSAOEffect() = default;
    ~SSAOEffect() override;

    void Initialize(ID3D11Device *device, UINT width, UINT height) override;
    void Resize(ID3D11Device *device, UINT width, UINT height) override;

    void Process(ID3D11DeviceContext      *dc,
                 ID3D11ShaderResourceView *inputSRV,
                 ID3D11RenderTargetView   *outputRTV) override;

    const char *GetName() const override { return "SSAO"; }

    void UpdateParams(const GpuData::CbSSAOData &data) { m_cbData = data; }
    void UpdateBlurParams(float depthSigma) { m_blurDepthSigma = depthSigma; }

private:
    GpuData::CbSSAOData m_cbData{};
    float               m_blurDepthSigma = 0.2f;

    struct Resources
    {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psRaw;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psBlur;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psComposite;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbSSAO;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbBlur;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_opaqueBlendState;
    std::unique_ptr<Resources>               m_res;

    RenderTargetHandle m_rawAO;
    RenderTargetHandle m_blurH;
    RenderTargetHandle m_blurV;
    UINT               m_width = 0, m_height = 0;

    void CreateTargets(UINT width, UINT height);
    void ReleaseTargets();
    void CopyInputToOutput(ID3D11DeviceContext      *dc,
                           ID3D11ShaderResourceView *inputSRV,
                           ID3D11RenderTargetView   *outputRTV);
};
