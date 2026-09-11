// 法線プリパス専用の軽量シェーダー(ShadowMapShader と同型)。
#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

class NormalPrepassShader : public Shader
{
public:
    NormalPrepassShader(ID3D11Device* device);
    ~NormalPrepassShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;
};
