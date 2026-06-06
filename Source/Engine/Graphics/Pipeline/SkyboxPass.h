#pragma once
#include "Engine/Graphics/Pipeline/Core/IRenderPass.h"
#include <wrl.h>
#include <DirectXMath.h>

/**
 * @brief フルスクリーン三角形でスカイボックスを描画するパス。
 * @details
 *  頂点バッファなし・Z=1.0 専用ステートを使い、
 *  不透明パスが描いた「空き領域」にだけ空を描く。
 */
class SkyboxPass : public IRenderPass
{
public:
    void Initialize(const RenderServices& services) override;
    void Execute(const RenderFrameContext& ctx) override;
    const char* GetName() const override { return "SkyboxPass"; }

private:
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> _skyboxTex;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>       _vertexShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>        _pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer>             _cbSkybox;

    struct CbSkybox {
        DirectX::XMFLOAT4X4 invViewProj;
    };
};