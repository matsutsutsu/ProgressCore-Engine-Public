#pragma once
// ===========================================================================
// 概要: 輪郭線を描くシェーダー（shaders.json の customShaders の "Outline"）。
//
// 責務:
//   - 表面を裏返してカリングする（表を捨てる）ラスタライザで描き、End で既定に戻す
//   - 輪郭線の色と太さ（b1 の CbOutline）をマテリアルから送る
//
// 備考: 頂点シェーダーが法線の向きに頂点を押し出し、裏面だけを描くことで、
//       元のモデルの外側に線が見える。
// ===========================================================================
#include "Engine/Graphics/Shader/Core/Shader.h"

class OutlineShader : public IMaterialShader
{
public:
    OutlineShader(ID3D11Device *device);
    ~OutlineShader() override = default;

    void Begin(ID3D11DeviceContext *dc, const RenderServices &s, const RenderFrameContext &ctx) override;
    void Update(ID3D11DeviceContext *dc, const Model::Mesh &mesh, const MaterialData *overrideMaterial = nullptr) override;
    void End(ID3D11DeviceContext *dc, const RenderServices &s) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader>    vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>     pixelShader;
    Microsoft::WRL::ComPtr<ID3D11InputLayout>     inputLayout;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState; // 表面をカリングする
};