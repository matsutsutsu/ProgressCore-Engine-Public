#pragma once
#include <memory>
#include <vector>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <DirectXCollision.h> // BoundingFrustum用
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Core/RenderContext.h"
#include "Engine/Graphics/Renderer/Pipeline/RenderPipeline.h"

class ModelRenderer
{
public:
    ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() = default;

    // 毎フレームの描画実行（パイプラインを回す）
    void Render(const RenderContext& rc);

    // =========================================================
    // 舞台監督 (EnvironmentSystem) や 各Pass から呼ばれるAPI
    // =========================================================

    void BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc);
    void UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh);

    // 影関連のパラメータ設定・取得
    void SetShadowLightViewProjection(const DirectX::XMFLOAT4X4& vp) { _shadowLightVP = vp; }
    void SetShadowParams(float bias, const DirectX::XMFLOAT4& color) {
        _shadowBias = bias;
        _shadowColor = color;
    }

    float GetShadowBias() const { return _shadowBias; }
    const DirectX::XMFLOAT4& GetShadowColor() const { return _shadowColor; }
    const DirectX::XMFLOAT4X4& GetShadowLightVP() const { return _shadowLightVP; }

    const DirectX::BoundingFrustum& GetFrustum() const { return m_frustum; }

private:
    RenderPipeline _pipeline; // 全ての描画を司るパイプライン

    // シャドウマップパラメータ
    float _shadowBias = 0.0015f;
    DirectX::XMFLOAT4 _shadowColor = { 0.0f, 0.0f, 0.0f, 0.5f };
    DirectX::XMFLOAT4X4 _shadowLightVP = {
            1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
    };

    // スケルトン用定数バッファ
    ID3D11Buffer* _cbSkeletonBuffer = nullptr;
    static const int MAX_BONES = 256;
    struct CbSkeleton {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES];
    };
    CbSkeleton m_cbSkeletonCache = {};

    DirectX::BoundingFrustum m_frustum = {};

    // 内部初期化・解放関数
    void InitializeGlobalConstantBuffers(ID3D11Device* device);
    void UnbindAllResources(ID3D11DeviceContext* dc);
};