/// @file  ErrorShader.h
/// @brief 未登録シェーダー要求時のフォールバック（マゼンタ）シェーダー。
///        画面が紫になることで、シェーダー登録漏れ・ハッシュtypoを即座に可視化する。
#pragma once
#include "Engine/Graphics/Shader/Core/Shader.h"

/// @brief ShaderRegistry が未登録ハッシュに対して返す代替シェーダー。
/// @note  OpaquePass/TransparentPass のマテリアル経路に差し込めるよう
///        IMaterialShader を継承する。Update は意図的に空（材質を持たないため）。
/// @warning 本番ビルドでこのシェーダーが描画される＝登録漏れのバグ。放置しないこと。
class ErrorShader : public IMaterialShader
{
public:
    explicit ErrorShader(ID3D11Device* device);
    ~ErrorShader() override = default;

    void Begin(ID3D11DeviceContext* dc, const RenderServices& s, const RenderFrameContext& ctx) override;

    /// @brief 材質を持たないため意図的に空。AsMaterial() の非null性のためだけに存在する。
    void Update(ID3D11DeviceContext* dc, const Model::Mesh& mesh,
        const MaterialData* overrideMaterial = nullptr) override {
    }

    void End(ID3D11DeviceContext* dc, const RenderServices& s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> _vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  _ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  _inputLayout;
};