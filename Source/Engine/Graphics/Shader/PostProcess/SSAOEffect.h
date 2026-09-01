// SSAO(スクリーンスペース・アンビエントオクルージョン)ポストエフェクト。
// 役割      : 深度+法線から遮蔽を計算し、隙間・接地面・くぼみを暗くする。
// 責務      : ①遮蔽値計算 → ②③深度考慮ブラー(H/V) → ④シーン色へ合成。
// 位置付け  : IPostEffect チェーンの最前段(Bloom より前)。
// 設計方針  : Bloom と同型の多段パス。生ノイズはブラーで均してから合成する。
#pragma once
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

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;

    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "SSAO"; }

    void UpdateParams(const GpuData::CbSSAOData& data) { m_cbData = data; }
    void UpdateBlurParams(float depthSigma) { m_blurDepthSigma = depthSigma; }

private:
    GpuData::CbSSAOData m_cbData{};
    float m_blurDepthSigma = 0.2f;

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psRaw;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psBlur;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  psComposite;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbSSAO;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cbBlur;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_opaqueBlendState;
    std::unique_ptr<Resources> m_res;

    RenderTargetHandle m_rawAO;
    RenderTargetHandle m_blurH;
    RenderTargetHandle m_blurV;
    UINT m_width = 0, m_height = 0;

    void CreateTargets(UINT width, UINT height);
    void ReleaseTargets();
    void CopyInputToOutput(ID3D11DeviceContext* dc,
                           ID3D11ShaderResourceView* inputSRV,
                           ID3D11RenderTargetView* outputRTV);
};
