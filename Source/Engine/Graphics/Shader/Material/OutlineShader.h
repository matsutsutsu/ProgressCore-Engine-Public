#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

class OutlineShader : public Shader
{
public:
    OutlineShader(ID3D11Device* device);
    ~OutlineShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;
    void Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh) override;
    /// @details s.renderState を使ってラスタライザを復元する（Graphics::Instance() 除去済み）。
    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader>     vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>      pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>      inputLayout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>  m_rasterizerState;
};