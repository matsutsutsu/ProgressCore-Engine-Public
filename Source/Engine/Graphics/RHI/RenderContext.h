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

// ============================================================================
//  RenderContext : 旧 Shader/Renderer 互換のための統合ビュー（移行期のみ）
// ============================================================================
/**
 * @brief 旧 Shader / Renderer API 用のレガシー互換ビュー。
 * @details
 *  既存の Shader::Begin(rc) や PrimitiveRenderer::Render(rc, ...) 等が
 *  この型を要求しているため、移行期は MakeLegacyContext() で構築して渡す。
 *
 *  将来：Shader 側を services + ctx を取る形に更新したらこの構造体は削除する。
 */
struct RenderContext
{
    ID3D11DeviceContext* deviceContext = nullptr;
    const RenderState* renderState = nullptr;
    const Camera* camera = nullptr;
    LightManager* lightManager = nullptr;
    ShadowMap* shadowMap = nullptr;

    ModelRenderer* modelRenderer = nullptr;
    PostProcessManager* postProcess = nullptr;
    ParticleRenderer* particleRenderer = nullptr;
    PrimitiveRenderer* primitiveRenderer = nullptr;
    ShapeRenderer* shapeRenderer = nullptr;

    RenderQueue* renderQueue = nullptr;
    ResourceManager* resourceManager = nullptr;
    ShaderRegistry* shaderRegistry = nullptr;

    ID3D11RenderTargetView* targetRTV = nullptr;
};

/**
 * @brief Services + FrameContext から旧 RenderContext を構築する。
 * @details 各パスの Execute 冒頭で呼び、Shader 等の旧 API に渡す。
 *          Shader 側が更新されたら不要になる。
 */
inline RenderContext MakeLegacyContext(const RenderServices& s, const RenderFrameContext& ctx) noexcept
{
    RenderContext rc;
    rc.deviceContext = ctx.dc;
    rc.renderState = s.renderState;
    rc.camera = ctx.camera;
    rc.lightManager = s.lightManager;
    rc.shadowMap = s.shadowMap;
    rc.modelRenderer = s.modelRenderer;
    rc.postProcess = s.postProcess;
    rc.particleRenderer = s.particleRenderer;
    rc.primitiveRenderer = s.primitiveRenderer;
    rc.shapeRenderer = s.shapeRenderer;
    rc.renderQueue = s.renderQueue;
    rc.resourceManager = s.resources;
    rc.shaderRegistry = s.shaders;
    rc.targetRTV = ctx.targetRTV;
    return rc;
}