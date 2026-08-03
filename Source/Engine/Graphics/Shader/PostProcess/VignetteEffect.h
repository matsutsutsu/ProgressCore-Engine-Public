/// @file  VignetteEffect.h
/// @brief ビネット(画面端の減光/着色)ポストエフェクト。
#pragma once
#include "IPostEffect.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>

class VignetteEffect : public IPostEffect
{
public:
    VignetteEffect() = default;
    ~VignetteEffect() override = default;

    void Initialize(ID3D11Device* device, UINT width, UINT height) override;
    void Resize(ID3D11Device* device, UINT width, UINT height) override;
    void Process(ID3D11DeviceContext* dc,
                 ID3D11ShaderResourceView* inputSRV,
                 ID3D11RenderTargetView* outputRTV) override;

    const char* GetName() const override { return "Vignette"; }
    void UpdateParams(const GpuData::CbVignetteData& data) { m_cbData = data; }

private:
    GpuData::CbVignetteData m_cbData{};

    struct Resources {
        Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
        Microsoft::WRL::ComPtr<ID3D11Buffer>       cb;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
    };
    std::unique_ptr<Resources> m_res;
};
