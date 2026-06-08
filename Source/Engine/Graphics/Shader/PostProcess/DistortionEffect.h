#pragma once
#include "Engine/Graphics/RHI/RenderTarget.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Shader/Shared/PostProcessShared.h"
#include "IPostEffect.h"
#include <d3d11.h>
#include <memory>
#include <wrl/client.h>

class DistortionEffect : public IPostEffect {
public:
  DistortionEffect() = default;
  ~DistortionEffect() override;

  void Initialize(ID3D11Device *device, UINT width, UINT height) override;
  void Resize(ID3D11Device *device, UINT width, UINT height) override;

  void Process(ID3D11DeviceContext *dc, ID3D11ShaderResourceView *inputSRV,
      ID3D11RenderTargetView *outputRTV) override;

  const char *GetName() const override { return "Distortion"; }

  void SetEnabled(bool enabled) { _enabled = enabled; }
  void UpdateParams(const GpuData::CbDistortionData &data) { _cbData = data; }

private:
  bool                      _enabled = true;
  GpuData::CbDistortionData _cbData{};

  struct Resources {
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       cbDistortion;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
  };

  std::unique_ptr<Resources> _res;
  RenderTargetHandle         _intermediateRT;
};