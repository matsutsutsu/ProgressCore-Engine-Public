#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

class ShadowMapShader : public Shader
{
public:
    ShadowMapShader(ID3D11Device *device);
    ~ShadowMapShader() override = default;

    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;
    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;
};