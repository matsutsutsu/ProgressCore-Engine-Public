#include "Graphics.h"
#include <chrono>
#include <cmath>
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/SceneView/Camera.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Shader/Material/ErrorShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ParticleShader.h"
#include "Engine/Graphics/Shader/Material/NormalPrepassShader.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

// BuildDefaultRenderPasses で登録するパス
#include "Engine/Graphics/Pipeline/SetupPass.h"
#include "Engine/Graphics/Pipeline/ShadowPass.h"
#include "Engine/Graphics/Pipeline/NormalPrepass.h"
#include "Engine/Graphics/Pipeline/OpaquePass.h"
#include "Engine/Graphics/Pipeline/LodDebugPass.h"
#include "Engine/Graphics/Pipeline/SkyboxPass.h"
#include "Engine/Graphics/Pipeline/SceneColorGrabPass.h"
#include "Engine/Graphics/Pipeline/TransparentPass.h"
#include "Engine/Graphics/Pipeline/ParticlePass.h"
#include "Engine/Graphics/Pipeline/TrailPass.h"
#include "Engine/Graphics/Pipeline/PrimitivePass.h"
#include "Engine/Graphics/Pipeline/PostProcessPass.h"

#include "tracy/TracyD3D11.hpp"

// ---------------------------------------------------------------------------
// 初期化とパイプラインの組み立て
// ---------------------------------------------------------------------------

void Graphics::Initialize(HWND hWnd)
{
    // ---- 1. D3D11 のデバイスと RT ----
    m_gfxDevice.Initialize(hWnd);
    ID3D11Device *device = m_gfxDevice.GetDevice();

    // アスペクト比だけ実際のウィンドウサイズに合わせる（ゲームごとの値は後で BaseScene が設定する）
    float aspect = m_gfxDevice.GetWidth() / m_gfxDevice.GetHeight();
    m_mainCamera.SetPerspectiveFov(DirectX::XMConvertToRadians(45.0f), aspect, 0.1f, 1000.0f);

    // ---- 2. 描画サブシステム ----
    m_renderState       = std::make_unique<RenderState>(device);
    m_primitiveRenderer = std::make_unique<PrimitiveRenderer>(device);
    m_shapeRenderer     = std::make_unique<ShapeRenderer>(device);
    m_modelConstants    = std::make_unique<ModelConstantBuffers>(device);
    m_shadowMap         = std::make_unique<ShadowMap>(device, 2048, 2048);
    m_particleRenderer  = std::make_unique<ParticleRenderer>();
    m_particleRenderer->Initialize(device);
    m_trailRenderer = std::make_unique<TrailRenderer>();
    m_trailRenderer->Initialize(device);
    m_lightManager = std::make_unique<LightManager>();
    m_lightManager->Initialize(device);
    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(device, m_gfxDevice.GetWidthU(), m_gfxDevice.GetHeightU());

    // ModelConstantBuffers に CbScene を渡す（BindAllGlobalConstantBuffers でバインドする）
    m_modelConstants->SetSceneConstantBuffer(m_gfxDevice.GetSceneConstantBuffer());

    // ---- 3. シェーダー登録 ----
    ShaderRegistry &shaders = ShaderRegistry::Instance();

    // 未登録のシェーダーの代わりに描く ErrorShader を最初に登録する（並べ替えの番号が 0 になる）
    shaders.RegisterShader<ErrorShader>("Error"_hash);

    // マテリアルのシェーダー。エディタのシェーダー選択にこの順で出る
    shaders.RegisterMaterialShader("PBR");
    shaders.RegisterMaterialShader("Phong");
    shaders.RegisterMaterialShader("Lambert");
    shaders.RegisterMaterialShader("Basic");

    // 専用の C++ クラスを持つシェーダー（各パスがハッシュで取り出す）
    shaders.RegisterShader<ShadowMapShader>("ShadowMap"_hash);
    shaders.RegisterShader<ParticleShader>("Particle"_hash);
    shaders.RegisterShader<NormalPrepassShader>("NormalPrepass"_hash);

    // ---- 4. レンダーパイプライン ----
    SetupRenderPipeline();
}

void Graphics::SetupRenderPipeline()
{
    // パイプラインへ渡す永続サービスを埋める
    m_renderServices.device              = m_gfxDevice.GetDevice();
    m_renderServices.sceneConstantBuffer = m_gfxDevice.GetSceneConstantBuffer();
    m_renderServices.renderState         = m_renderState.get();
    m_renderServices.resources           = &ResourceManager::Instance();
    m_renderServices.shaders             = &ShaderRegistry::Instance();
    m_renderServices.modelConstants      = m_modelConstants.get();
    m_renderServices.primitiveRenderer   = m_primitiveRenderer.get();
    m_renderServices.shapeRenderer       = m_shapeRenderer.get();
    m_renderServices.particleRenderer    = m_particleRenderer.get();
    m_renderServices.trailRenderer       = m_trailRenderer.get();
    m_renderServices.shadowMap           = m_shadowMap.get();
    m_renderServices.renderQueue         = &RenderQueue::Instance();
    m_renderServices.lightManager        = m_lightManager.get();
    m_renderServices.postProcess         = m_postProcess.get();
    m_renderServices.screenWidth         = m_gfxDevice.GetWidthU();
    m_renderServices.screenHeight        = m_gfxDevice.GetHeightU();
    m_renderServices.tracyContext        = m_gfxDevice.GetTracyContext();

    // RenderQueue にも ShaderRegistry を渡し、ソート中にシングルトンを参照しないようにする
    RenderQueue::Instance().Initialize(m_renderServices.shaders);

    // パイプライン初期化（AddPass の前に必ず呼ぶ）
    m_pipeline.Initialize(m_renderServices);

    // レンダーパスの構築
    BuildDefaultRenderPasses();

    OutputDebugStringA("[Graphics] RenderPipeline initialized.\n");
}

// 既定の 3D シーン用のパス構成。登録順がそのまま実行順になる。
void Graphics::BuildDefaultRenderPasses()
{
    // TODO(2026-09-10): ローディング画面など軽い構成が要るシーン用に、別の Build*RenderPasses を用意して切り替えられるようにする

    m_pipeline.AddPass<SetupPass>();                         //  1. HDR バッファのクリアとバインド
    m_pipeline.AddPass<ShadowPass>();                        //  2. シャドウマップ
    m_pipeline.AddPass<NormalPrepass>();                     //  3. 深度と法線のプリパス。不透明の深度を先に決める
    m_pipeline.AddPass<OpaquePass>();                        //  4. 不透明（深度が決まっているので early-Z が効く）
    m_lodDebugPass = m_pipeline.AddPass<LodDebugPass>();     //  5. LOD の段の色分け（既定はオフ。エディタで切り替える）
    m_pipeline.AddPass<SkyboxPass>();                        //  6. スカイボックス
    m_pipeline.AddPass<SceneColorGrabPass>();                //  7. 屈折用にシーンカラーを写し取る
    m_pipeline.AddPass<TransparentPass>();                   //  8. 半透明（距離でソート）
    m_pipeline.AddPass<ParticlePass>();                      //  9. パーティクル
    m_pipeline.AddPass<TrailPass>();                         // 10. 武器トレイル。ブルームに拾わせるためポストプロセスより前
    m_pipeline.AddPass<PrimitivePass>();                     // 11. デバッグプリミティブ
    m_pipeline.AddPass<PostProcessPass>();                   // 12. ポストプロセス・最終出力
}

// ---------------------------------------------------------------------------
// 毎フレーム
// ---------------------------------------------------------------------------

// CbScene を書くのはここだけ。EnvironmentSystem はバインドするだけ。
void Graphics::Update()
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    ShaderRegistry::Instance().UpdateHotReload(m_gfxDevice.GetDevice());
#endif

    // ------------------------------------------------------------------
    // カメラのアスペクト比をバックバッファの実寸に合わせる。
    //   CameraBrainSystem は GetAspect() を読んでそのまま書き戻すだけなので、
    //   ここで合わせないと、ウィンドウサイズが変わったときに絵が横に伸びる。
    //   アスペクトを更新するのはここだけにしておき、リサイズ処理を足してもカメラ側は触らずに済むようにする。
    //   （fovY が 0 のうちは透視投影がまだ設定されていないので触らない）
    // ------------------------------------------------------------------
    if (m_gfxDevice.GetHeight() > 0.0f && m_mainCamera.GetFovY() > 0.0f)
    {
        const float aspect = m_gfxDevice.GetWidth() / m_gfxDevice.GetHeight();
        if (std::fabs(aspect - m_mainCamera.GetAspect()) > 1e-4f)
        {
            m_mainCamera.SetPerspectiveFov(m_mainCamera.GetFovY(), aspect,
                                           m_mainCamera.GetNear(), m_mainCamera.GetFar());
        }
    }

    // CbLight (b9) を GPU へ送ってバインドする
    if (m_lightManager)
    {
        m_lightManager->Bind(m_gfxDevice.GetContext(), SLOT_CB_LIGHT);
    }

    if (!m_gfxDevice.GetSceneConstantBuffer()) return;

    // CbScene を組み立てる
    GpuData::CbScene data{};

    // XMFLOAT4X4 は行優先で並ぶので、HLSL の row_major float4x4 とそのまま一致する
    data.viewProjection = m_mainCamera.GetViewProjectionMatrix();
    data.gView          = m_mainCamera.GetView();
    data.gProjection    = m_mainCamera.GetProjection();

    const DirectX::XMFLOAT3 &pos = m_mainCamera.GetEye();
    data.cameraPosition          = {pos.x, pos.y, pos.z, 1.0f};

    if (m_lightManager)
    {
        const DirectionalLight &dir = m_lightManager->GetDirectionalLight();
        data.lightDirection         = {dir.direction.x, dir.direction.y, dir.direction.z, 0.0f};
        data.lightColor             = {dir.color.x, dir.color.y, dir.color.z, dir.intensity};
    }

    {
        static const auto s_start  = std::chrono::high_resolution_clock::now();
        static auto       s_prev   = s_start;
        const auto        now      = std::chrono::high_resolution_clock::now();
        const float       timeSec  = std::chrono::duration<float>(now - s_start).count();
        const float       deltaSec = std::chrono::duration<float>(now - s_prev).count();
        s_prev                     = now;
        data.gTimeParams           = {timeSec, deltaSec, sinf(timeSec), 0.0f};
    }

    {
        const float sw     = m_gfxDevice.GetWidth();
        const float sh     = m_gfxDevice.GetHeight();
        data.gScreenParams = {sw, sh, (sw > 0 ? 1.0f / sw : 0.0f), (sh > 0 ? 1.0f / sh : 0.0f)};
    }

    m_gfxDevice.UpdateSceneConstantBuffer(&data, sizeof(data));
}

void Graphics::Render(const RenderFrameContext &ctx)
{
    // このスコープの D3D11 の描画コマンドを、Tracy の GPU タイムラインに記録する
    TracyD3D11Zone(m_gfxDevice.GetTracyContext(), "Graphics::Render Pipeline (GPU)");

    m_pipeline.Execute(ctx);
    UnbindAllResources(ctx.dc);
}

RenderFrameContext Graphics::MakeFrameContext(ID3D11RenderTargetView *overrideRTV) noexcept
{
    RenderFrameContext ctx;
    ctx.dc             = m_gfxDevice.GetContext();
    ctx.camera         = &m_mainCamera;
    ctx.targetRTV      = (overrideRTV != nullptr) ? overrideRTV : m_gfxDevice.GetGameRTV();
    ctx.targetDSV      = m_gfxDevice.GetDepthStencilView();
    ctx.sceneDepthSRV  = m_gfxDevice.GetDepthSRV();
    ctx.sceneNormalSRV = m_sceneNormalSRV;
    ctx.skyboxSRV      = m_pendingSkyboxSRV; // EnvironmentSystem が決めた今の空
    ctx.skyIntensity   = m_pendingSkyIntensity;
    ctx.viewportW      = m_gfxDevice.GetWidthU();
    ctx.viewportH      = m_gfxDevice.GetHeightU();
    return ctx;
}

void Graphics::UnbindAllResources(ID3D11DeviceContext *dc)
{
    ID3D11Buffer             *nullCB      = nullptr;
    ID3D11SamplerState       *nullSampler = nullptr;
    ID3D11ShaderResourceView *nullSRV     = nullptr;

    dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &nullCB);
    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SKELETON, 1, &nullCB);
    dc->VSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_LIGHT, 1, &nullCB);
    dc->VSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);
    dc->PSSetConstantBuffers(SLOT_CB_SHADOW, 1, &nullCB);
    dc->PSSetSamplers(0, 1, &nullSampler);
    dc->PSSetSamplers(SLOT_SMP_SHADOW, 1, &nullSampler);
    dc->PSSetShaderResources(SLOT_SRV_SHADOW, 1, &nullSRV);
}

// ---------------------------------------------------------------------------
// 終了・画面への表示・デバイスロストからの復帰
// ---------------------------------------------------------------------------

void Graphics::Shutdown()
{
    // ==============================================================
    // 1. シングルトン（ResourceManager・ShaderRegistry）が持つ GPU リソースを解放する。
    //    デバイスを壊す前に行う必要がある
    // ==============================================================
    ResourceManager::Instance().ClearAll();
    ShaderRegistry::Instance().ClearAll();

    // ==============================================================
    // 2. パイプラインとパスを破棄する
    // ==============================================================
    if (m_gfxDevice.GetContext())
    {
        UnbindAllResources(m_gfxDevice.GetContext());
    }

    // 各パスの Shutdown が走る
    m_pipeline.Shutdown();
    m_lodDebugPass = nullptr; // パスは破棄されたので指さない（デバイスロストからの復帰で作り直される）

    // 描画サブシステムを作ったのと逆の順に解放する
    m_postProcess.reset();
    m_lightManager.reset();
    m_trailRenderer.reset();
    m_particleRenderer.reset();
    m_shadowMap.reset();
    m_shapeRenderer.reset();
    m_primitiveRenderer.reset();
    m_modelConstants.reset();
    m_renderState.reset();

    // D3D11 のデバイスは最後に解放する
    m_gfxDevice.Shutdown();
}

// DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET を受け取ったら false を返す
bool Graphics::Present(UINT syncInterval)
{
    HRESULT hr = m_gfxDevice.Present(syncInterval);

    // ウィンドウが完全に隠れている。描画は飛ばされるがエラーではない
    if (hr == DXGI_STATUS_OCCLUDED) return true;

    // デバイスロスト
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        // 原因のコードをログに残す
        const HRESULT reason = m_gfxDevice.GetDeviceRemovedReason();
        char          buf[128];
        sprintf_s(buf,
                  "[Graphics] !!! Device lost detected. Reason=0x%08X "
                  "(HUNG=0x887A0006 RESET=0x887A0007 REMOVED=0x887A0005 DRIVER=0x887A0020)\n",
                  static_cast<unsigned int>(reason));
        OutputDebugStringA(buf);
        return false;
    }

    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    return true;
}

// HWND を控え、Shutdown()（ShaderRegistry・ResourceManager も空にする）の後に
// Initialize() で新しいデバイスから全部作り直す。
bool Graphics::RecoverFromDeviceLost()
{

    OutputDebugStringA("[Graphics] ===== Device Lost Recovery: START =====\n");

    // ---- 1. HWND を控える ----
    // Shutdown() しても GraphicsDevice の m_hWnd は消えない
    HWND hWnd = m_gfxDevice.GetWindowHandle();
    if (!hWnd)
    {
        OutputDebugStringA("[Graphics] RecoverFromDeviceLost: HWND が無効です。復帰失敗。\n");
        return false;
    }

    // ---- 2. D3D のリソースを全部解放する ----
    // Shutdown() がまとめて行う:
    //   - ResourceManager::ClearAll()  テクスチャ・メッシュなど全 GPU リソース
    //   - ShaderRegistry::ClearAll()   シェーダーのキャッシュとファクトリ
    //   - パイプライン → 描画サブシステム → GraphicsDevice の順で解放
    Shutdown();

    // ---- 3. 新しいデバイスで作り直す ----
    // Initialize() が行う:
    //   - 新しい D3D11 デバイスとスワップチェーンを作る
    //   - シェーダーのファクトリを登録し直す
    //   - SetupRenderPipeline() でパイプラインとパスを組み直す
    // シェーダー本体は、GetShader() が最初に呼ばれたときに .cso から読み直される
    Initialize(hWnd);

    OutputDebugStringA("[Graphics] ===== Device Lost Recovery: COMPLETE =====\n");
    return true;
}
