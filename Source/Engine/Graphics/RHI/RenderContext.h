#pragma once
#include <d3d11.h>
#include <cstdint>

// ============================================================================
//  前方宣言
// ============================================================================
class ResourceManager;
class ShaderRegistry;
class RenderState;
class Camera;
class LightManager;
class ShadowMap;
class ModelRenderer;
class ParticleRenderer;
class PrimitiveRenderer;
class ShapeRenderer;
class PostProcessManager;
class RenderQueue;

// ============================================================================
//  RenderServices : エンジン起動から終了まで存続する永続サービス群
// ============================================================================
/**
 * @brief 永続サービス。各 RenderPass が Initialize 時に受け取る。
 * @details
 *  ポインタは Initialize 後にエンジン終了まで有効。
 *  「ポインタが変わらないもの」をここに集約する（中身は変化してよい）。
 *  Singleton への直接アクセスを排除し、テスト時のモック注入を可能にする。
 */
struct RenderServices
{
    // ---- RHI / アセット ----
    ID3D11Device* device = nullptr;
    /// @brief CbScene 定数バッファ (b8)。Graphics が所有し lifetime を管理する。
    ID3D11Buffer* sceneConstantBuffer = nullptr;

    ResourceManager* resources = nullptr;
    ShaderRegistry* shaders = nullptr;
    const RenderState* renderState = nullptr;

    // ---- 描画サブシステム（常駐レンダラ） ----
    ModelRenderer* modelRenderer = nullptr;
    ParticleRenderer* particleRenderer = nullptr;
    PrimitiveRenderer* primitiveRenderer = nullptr;
    ShapeRenderer* shapeRenderer = nullptr;
    PostProcessManager* postProcess = nullptr;

    // ---- シーン情報（オブジェクト自体は永続。中身はフレーム毎に更新される） ----
    LightManager* lightManager = nullptr;
    ShadowMap* shadowMap = nullptr;
    RenderQueue* renderQueue = nullptr;
};

// ============================================================================
//  RenderFrameContext : 1 フレームのみ有効な揮発データ
// ============================================================================
/**
 * @brief フレーム単位で再構築される揮発コンテキスト。
 * @details
 *  毎フレーム差し替わる「ポインタ」と「値」を集約。
 *  パイプライン Execute 時のみ有効。次フレームで内容は無効化される前提。
 */
struct RenderFrameContext
{
    // ---- アクティブな描画コンテキスト ----
    ID3D11DeviceContext* dc = nullptr;
    const Camera* camera = nullptr;

    // ---- 出力先（パイプライン最終出力。途中パスで切り替わる場合あり） ----
    ID3D11RenderTargetView* targetRTV = nullptr;
    ID3D11DepthStencilView* targetDSV = nullptr;

    // ---- メタ情報 ----
    float    deltaTime = 0.0f;
    uint64_t frameIndex = 0;
    uint32_t viewportW = 0;
    uint32_t viewportH = 0;
};
