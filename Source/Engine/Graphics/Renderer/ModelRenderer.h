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

#include "Engine/Graphics/Resource/ResourceManager.h"

class ModelRenderer
{
public:
    ModelRenderer(ID3D11Device* device);
    ~ModelRenderer() {}

    // 描画登録
    void Draw(ShaderId                     shaderId,
        const Model                       *model,
        const DirectX::XMFLOAT4X4& worldMatrix,
        const std::vector<MaterialHandle> &overrideMaterials,
        const DirectX::XMFLOAT4           &customParams = {0.0f, 0.0f, 0.0f, 0.0f}
    ); 

    // 描画実行
    void Render(const RenderContext& rc);

    // 登録されている全シェーダーのGUIを描画する
    void DrawAllShaderGUI();

    // フレームの開始時に呼ぶ（リストのクリア用）
    void BeginFrame();

    // 視錐台を取得
    const DirectX::BoundingFrustum& GetFrustum() const { return m_frustum; }

    // === シェーダーリフレクション対応の新しいAPI ===

    // グローバル定数バッファに値を設定（全シェーダーで共有されるCbScene, CbSkeletonなど）
    template<typename T>
    bool SetGlobalConstantValue(const std::string& bufferName, const std::string& varName, const T& value)
    {
        auto it = globalConstantBuffers.find(bufferName);
        if (it == globalConstantBuffers.end()) return false;
        return it->second.SetValue(varName, value);
    }

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

    struct DrawInfo
    {
        ShaderId                shaderId = ShaderId::Phong;
        const Model        *model       = nullptr;
        DirectX::XMFLOAT4X4 worldMatrix = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
        };
        // マテリアルオーバーライド用
        // ゲームの仕様上、1モデルに対するマテリアル上書きは最大4つもあれば十分
        std::array<MaterialHandle, 4> overrideMaterials;
        uint8_t                       overrideCount = 0; // 上書きしている数

        // 個体ごとの汎用パラメータ ディゾルブやフラッシュの色など、シェーダー側で用途を決める
        DirectX::XMFLOAT4 customParams = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    struct TransparencyDrawInfo
    {
        ShaderId            shaderId = ShaderId::Phong;
        const Model::Mesh*  mesh = nullptr;
        float               distance = 0.0f;
        DirectX::XMFLOAT4X4 worldMatrix = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1
        };
        MaterialHandle      overrideMaterial;

        // 半透明オブジェクト用の個体パラメータ保持用
        DirectX::XMFLOAT4 customParams = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    // シェーダー管理
    std::unique_ptr<Shader> shaders[static_cast<int>(ShaderId::EnumCount)];

    // 描画キュー
    std::vector<DrawInfo> drawInfos;
    std::vector<TransparencyDrawInfo> transparencyDrawInfos;

    // === リフレクション対応 ===

    // GPUインスタンシング用のデータ
    InstanceBuffer _instanceBuffer;

    // グローバル定数バッファ（全シェーダーで共有）
    std::map<std::string, ShaderReflect::AutoConstantBuffer> globalConstantBuffers;

    // グローバル定数バッファのリフレクション情報
    ShaderReflect::ReflectionData globalReflection;

    // ビューフラスタム
    DirectX::BoundingFrustum m_frustum = {};

    // グローバル定数バッファの初期化
    void InitializeGlobalConstantBuffers(ID3D11Device* device);

    // スケルトン情報を更新（リフレクションベース）
    void UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh);

    // リソース解除処理
    void UnbindAllResources(ID3D11DeviceContext* dc);
};