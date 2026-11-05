#pragma once
// ===========================================================================
// 概要: 画面全体を覆う三角形1枚で、スカイボックスを一番奥（Z = 1.0）に描くパス
//
// 責務:
//   - ビュー・プロジェクションの逆行列を定数バッファに書き、ピクセルごとの視線方向を求めさせる
//   - EnvironmentSystem が決めた空のテクスチャ（ctx.skyboxSRV）を、明るさの倍率（ctx.skyIntensity）を掛けて描く
//
// 備考: 頂点バッファは使わず、頂点シェーダーが SV_VertexID から三角形を作る
//       深度は書かずに LESS_EQUAL で比べるので、不透明の後に描けば何もない所にだけ空が出る
//       空のテクスチャは EnvironmentSystem が読む。このパスでは読まない
// ===========================================================================
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include <wrl.h>
#include <DirectXMath.h>

class SkyboxPass : public IRenderPass
{
public:
    void Initialize(const RenderServices &services) override;
    void Execute(const RenderFrameContext &ctx) override;

private:
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>  m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer>       m_cbSkybox;

    // SkyboxVS.hlsl / SkyboxPS.hlsl の CbSkybox と同じ並び
    struct CbSkybox
    {
        DirectX::XMFLOAT4X4 invViewProj;  // (ビュー × プロジェクション) の逆行列
        float               skyIntensity; // 空の明るさの倍率
        float               padding[3];
    };
};
