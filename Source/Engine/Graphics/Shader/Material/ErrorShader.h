#pragma once
// ===========================================================================
// 概要: 登録されていないシェーダーを要求されたとき、代わりにマゼンタ一色で描くシェーダー
//
// 責務:
//   - モデルの入力レイアウトで頂点を変換し、ピクセルシェーダーでマゼンタを出す
//
// 備考: ShaderRegistry::GetShader が未登録のハッシュに返す。マゼンタなので、登録漏れや打ち間違いにすぐ気付ける
//       マテリアルはシェーダーをハッシュで保存するので、登録名を変える・消すと、使っていたメッシュがここへ来る
//       登録は Graphics::Initialize で、ほかのシェーダーより先に行う
// ===========================================================================
#include "Engine/Graphics/Shader/Core/Shader.h"

class ErrorShader : public IMaterialShader
{
public:
    explicit ErrorShader(ID3D11Device *device);
    ~ErrorShader() override = default;

    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;

    // マテリアルを使わないので何もしない
    void Update(ID3D11DeviceContext *dc, const Model::Mesh &mesh,
                const MaterialData *overrideMaterial = nullptr) override
    {
    }

    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_ps;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>  m_inputLayout;
};