#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"
#include <string>

// すべてのモデル描画用シェーダーを統合する汎用クラス
class MaterialShader : public Shader {
public:
    // CSOファイルのパスを受け取るコンストラクタ
    MaterialShader(ID3D11Device* device, const std::string& vsPath, const std::string& psPath);
    ~MaterialShader() override = default;

    void Begin(const RenderContext& rc) override;
    void Update(const RenderContext& rc, const Model::Mesh& mesh) override;
    void End(const RenderContext& rc) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  inputLayout;
};