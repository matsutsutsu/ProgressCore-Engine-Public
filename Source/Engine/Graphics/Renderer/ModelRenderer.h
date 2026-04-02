#pragma once
#include <memory>
#include <vector>
#include <array>
#include <wrl.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include "Engine/Graphics/Resource/Model.h"
#include "Engine/Graphics/Shader/Material/Core/Shader.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"
#include "Engine/Graphics/Shader/Material/Core/ShaderId.h"
#include "Engine/Graphics/Renderer/InstanceBuffer.h"
#include "Engine/Graphics/Core/PipelineState.h"

#include "Engine/Graphics/Resource/ResourceManager.h"

class ModelRenderer
{
public:
    ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() {}

    // 描画実行
    void Render(const RenderContext& rc);

    // フレームの開始時に呼ぶ（リストのクリア用）
    //void BeginFrame();

    // 視錐台を取得
    const DirectX::BoundingFrustum& GetFrustum() const { return m_frustum; }

    // === シェーダーリフレクション対応の新しいAPI ===


    // デバッグ：リフレクション情報を出力
    void PrintGlobalReflectionInfo() const;

    // 全シェーダーのリフレクション情報を出力
    void PrintAllShaderReflectionInfo() const;

    // 影描画用の登録関数
    void DrawShadow(const Model* model, const Model::Mesh* mesh, const DirectX::XMFLOAT4X4& worldMatrix);

    // 影描画の実行（ShadowMapRenderSystemから呼ぶ）
    void RenderShadowPass(const RenderContext& rc);

    // シャドウマップ用のライト行列をセットする関数
    void SetShadowLightViewProjection(const DirectX::XMFLOAT4X4& vp) { _shadowLightVP = vp; }

    // シャドウマップの値のセッター
    void SetShadowParams(float bias, const DirectX::XMFLOAT4& color) {
        _shadowBias = bias;
        _shadowColor = color;
    }

    // =========================================================
    // 舞台監督 (EnvironmentSystem) から呼べるように public に公開
    // =========================================================
    
    // 全グローバル定数バッファを更新
    void UpdateAllGlobalConstantBuffers(ID3D11DeviceContext *dc);

    // 全グローバル定数バッファをバインド
    void BindAllGlobalConstantBuffers(ID3D11DeviceContext *dc);

private:

    // パラメータ保存用
    float _shadowBias = 0.0015f;
    DirectX::XMFLOAT4 _shadowColor = { 0.0f, 0.0f, 0.0f, 0.5f };

    // シャドウマップ用のライト行列保存用
    DirectX::XMFLOAT4X4 _shadowLightVP = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
    };

    // CbSkeletonバッファへのポインタをキャッシュしておく変数
    ID3D11Buffer* _cbSkeletonBuffer = nullptr;


    // ShadowMap用の登録リスト
    struct ShadowDrawInfo {
        const Model* model = nullptr;
        const Model::Mesh* mesh = nullptr;
        DirectX::XMFLOAT4X4 worldMatrix = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
    };
    std::vector<ShadowDrawInfo> shadowDrawInfos;

    // 従来の構造体（後方互換性のため残す）
    struct CbScene
    {
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4   lightDirection;
        DirectX::XMFLOAT4   lightColor;
        DirectX::XMFLOAT4   cameraPosition;
        DirectX::XMFLOAT4X4 view;
        DirectX::XMFLOAT4X4 projection;
    };

    // 骨の最大数（シェーダー側と合わせる。例: 256）
    static const int MAX_BONES = 256;

    struct CbSkeleton
    {
        DirectX::XMFLOAT4X4 boneTransforms[MAX_BONES];
    };

    // --- 巨大なデータはスタックに置かず、メンバ変数として再利用する（スクラッチバッファ） ---
    CbSkeleton m_cbSkeletonCache = {};


    // === リフレクション対応 ===

    // GPUインスタンシング用のデータ
    InstanceBuffer _instanceBuffer;

    // グローバル定数バッファのリフレクション情報
    ShaderReflect::ReflectionData globalReflection;

    // ビューフラスタム
    DirectX::BoundingFrustum m_frustum = {};


    // モデル描画用の3つの金型
    PipelineState _psoOpaque;
    PipelineState _psoTransparent;
    PipelineState _psoShadow;

    // グローバル定数バッファの初期化
    void InitializeGlobalConstantBuffers(ID3D11Device* device);

    // スケルトン情報を更新（リフレクションベース）
    void UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh);

    // リソース解除処理
    void UnbindAllResources(ID3D11DeviceContext* dc);
};