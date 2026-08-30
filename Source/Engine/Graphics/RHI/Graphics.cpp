#include "Graphics.h"
#include <chrono>
#include <cmath>
#include "Engine/Graphics/Shader/ShaderManifest.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/Camera.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"

#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ParticleShader.h"

// デバッグ用のエラーシェーダー（シェーダーコンパイル失敗時にこれを描画する）
#include "Engine/Graphics/Shader/Material/ErrorShader.h"

#include "tracy/TracyD3D11.hpp"

// ============================================================
//  Initialize
// ============================================================
void Graphics::Initialize(HWND hWnd)
{
    // ---- Phase 1: D3D11 RHI 層 ----
    m_gfxDevice.Initialize(hWnd);
    ID3D11Device* device = m_gfxDevice.GetDevice();

    // Camera::Camera() がデフォルト値で初期化済み。
    // アスペクト比だけ実際のウィンドウサイズで上書きする（BaseScene が後でゲーム固有値を設定する）。
    float aspect = m_gfxDevice.GetWidth() / m_gfxDevice.GetHeight();
    m_mainCamera.SetPerspectiveFov(DirectX::XMConvertToRadians(45.0f), aspect, 0.1f, 1000.0f);

    // ---- Phase 2: 描画サブシステム生成 ----
    m_renderState = std::make_unique<RenderState>(device);
    m_primitiveRenderer = std::make_unique<PrimitiveRenderer>(device);
    m_shapeRenderer = std::make_unique<ShapeRenderer>(device);
    m_modelRenderer = std::make_unique<ModelRenderer>(device);
    m_shadowMap = std::make_unique<ShadowMap>(device, 2048, 2048);
    m_particleRenderer = std::make_unique<ParticleRenderer>();
    m_particleRenderer->Initialize(device);
    m_lightManager = std::make_unique<LightManager>();
    m_lightManager->Initialize(device);
    m_postProcess = std::make_unique<PostProcessManager>();
    m_postProcess->Initialize(device, m_gfxDevice.GetWidthU(), m_gfxDevice.GetHeightU());

    // ModelRenderer に CbScene ポインタを渡す（BindAllGlobalConstantBuffers 用）
    m_modelRenderer->SetSceneConstantBuffer(m_gfxDevice.GetSceneConstantBuffer());

    // ---- Phase 3: シェーダー登録 ----
    auto& reg = ShaderRegistry::Instance();

    //reg.RegisterShader<ErrorShader>("Error"_hash);  

    //reg.RegisterMaterialShader("PBR"_hash,
    //    "Assets/Shader/PBR_VS.cso", L"Shader/Material/PBR/PBR_VS.hlsl",
    //    "Assets/Shader/PBR_PS.cso", L"Shader/Material/PBR/PBR_PS.hlsl");
    //reg.RegisterMaterialShader("Phong"_hash,
    //    "Assets/Shader/PhongVS.cso", L"Shader/Material/Phong/PhongVS.hlsl",
    //    "Assets/Shader/PhongPS.cso", L"Shader/Material/Phong/PhongPS.hlsl");
    //reg.RegisterMaterialShader("Lambert"_hash,
    //    "Assets/Shader/LambertVS.cso", L"Shader/Material/Lambert/LambertVS.hlsl",
    //    "Assets/Shader/LambertPS.cso", L"Shader/Material/Lambert/LambertPS.hlsl");
    //reg.RegisterMaterialShader("Basic"_hash,
    //    "Assets/Shader/BasicVS.cso", L"Shader/Material/Basic/BasicVS.hlsl",
    //    "Assets/Shader/BasicPS.cso", L"Shader/Material/Basic/BasicPS.hlsl");
    //reg.RegisterMaterialShader("Toon"_hash,
    //    "Assets/Shader/ToonVS.cso", L"Shader/Material/Toon/ToonVS.hlsl",
    //    "Assets/Shader/ToonPS.cso", L"Shader/Material/Toon/ToonPS.hlsl");
    //reg.RegisterShader<OutlineShader>("Outline"_hash);
    //reg.RegisterShader<ShadowMapShader>("ShadowMap"_hash);

    //// Graphics.cpp の Phase 3 に追加
    //reg.RegisterShader<ParticleShader>("Particle"_hash);

    // マテリアルは Assets/Shader/shaders.json に1行足すだけ（C++・再ビルド不要）
    ShaderManifest::LoadAndRegister("Assets/Shader/shaders.json",
        ShaderRegistry::Instance());

    // ---- Phase 4: レンダリングパイプライン構築 ----
    SetupRenderPipeline();
}

void Graphics::SetupRenderPipeline()
{
    // RenderServices を構築してキャッシュ
    m_renderServices.device = m_gfxDevice.GetDevice();
    m_renderServices.sceneConstantBuffer = m_gfxDevice.GetSceneConstantBuffer();
    m_renderServices.renderState = m_renderState.get();
    m_renderServices.resources = &ResourceManager::Instance();
    m_renderServices.shaders = &ShaderRegistry::Instance();
    m_renderServices.modelRenderer = m_modelRenderer.get();
    m_renderServices.primitiveRenderer = m_primitiveRenderer.get();
    m_renderServices.shapeRenderer = m_shapeRenderer.get();
    m_renderServices.particleRenderer = m_particleRenderer.get();
    m_renderServices.shadowMap = m_shadowMap.get();
    m_renderServices.renderQueue = &RenderQueue::Instance();
    m_renderServices.lightManager = m_lightManager.get();
    m_renderServices.postProcess = m_postProcess.get();
    m_renderServices.screenWidth = m_gfxDevice.GetWidthU();
    m_renderServices.screenHeight = m_gfxDevice.GetHeightU();

    // (I) RenderQueue に ShaderRegistry を注入（ホットパス singleton ゼロ）
    RenderQueue::Instance().Initialize(m_renderServices.shaders);

    // パイプライン初期化（AddPass の前に必ず呼ぶ）
    m_pipeline.Initialize(m_renderServices);

    // レンダーパスの構築
	m_pipeline.BuildDefaultPipeline();
   
    OutputDebugStringA("[Graphics] RenderPipeline initialized.\n");
}

// ============================================================
//  Update（毎フレーム）
//  CbScene の書き込み権限はここのみ。EnvironmentSystem は上書きしない。
// ============================================================
void Graphics::Update()
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    ShaderRegistry::Instance().UpdateHotReload(m_gfxDevice.GetDevice());
#endif

    // ------------------------------------------------------------------
    // カメラのアスペクト比をバックバッファの実寸へ追従させる。
    //   CameraBrainSystem は GetAspect() を読んでそのまま書き戻すだけなので、
    //   初期化時以外にアスペクトを更新する箇所が存在せず、ウィンドウサイズが
    //   変わると絵が横伸びしていた。ここを唯一の更新点にしておけば、
    //   将来リサイズ処理を足したときにカメラ側は自動で追従する。
    //   （fovY == 0 は正射影の番兵。その場合は触らない）
    // ------------------------------------------------------------------
    if (m_gfxDevice.GetHeight() > 0.0f && m_mainCamera.GetFovY() > 0.0f) {
        const float aspect = m_gfxDevice.GetWidth() / m_gfxDevice.GetHeight();
        if (std::fabs(aspect - m_mainCamera.GetAspect()) > 1e-4f) {
            m_mainCamera.SetPerspectiveFov(m_mainCamera.GetFovY(), aspect,
                                           m_mainCamera.GetNear(), m_mainCamera.GetFar());
        }
    }

    // CbLight (b9) を GPU に転送・バインド（毎フレーム最初に実行）
    if (m_lightManager) {
        m_lightManager->Bind(m_gfxDevice.GetContext(), SLOT_CB_LIGHT);
    }

    if (!m_gfxDevice.GetSceneConstantBuffer()) return;

    // CbScene を構築（唯一の書き込み箇所）
    GpuData::CbScene data{};

    // XMFLOAT4X4 は row_major ストレージ = HLSL の row_major float4x4 と一致
    data.viewProjection = m_mainCamera.GetViewProjectionMatrix();
    data.gView          = m_mainCamera.GetView();
    data.gProjection    = m_mainCamera.GetProjection();

    const DirectX::XMFLOAT3& pos = m_mainCamera.GetEye();
    data.cameraPosition = { pos.x, pos.y, pos.z, 1.0f };

    if (m_lightManager) {
        const DirectionalLight& dir = m_lightManager->GetDirectionalLight();
        data.lightDirection = { dir.direction.x, dir.direction.y, dir.direction.z, 0.0f };
        data.lightColor = { dir.color.x,     dir.color.y,     dir.color.z, dir.intensity };
    }

    {
        static const auto s_start = std::chrono::high_resolution_clock::now();
        static auto s_prev = s_start;
        const auto now = std::chrono::high_resolution_clock::now();
        const float timeSec  = std::chrono::duration<float>(now - s_start).count();
        const float deltaSec = std::chrono::duration<float>(now - s_prev).count();
        s_prev = now;
        data.gTimeParams = { timeSec, deltaSec, sinf(timeSec), 0.0f };
    }

    {
        const float sw = m_gfxDevice.GetWidth();
        const float sh = m_gfxDevice.GetHeight();
        data.gScreenParams = { sw, sh, (sw > 0 ? 1.0f / sw : 0.0f), (sh > 0 ? 1.0f / sh : 0.0f) };
    }

    m_gfxDevice.UpdateSceneConstantBuffer(&data, sizeof(data));
}

// ============================================================
//  Render（毎フレーム） ← 旧 ModelRenderer::Render
// ============================================================
void Graphics::Render(const RenderFrameContext& ctx)
{
    // このスコープ内の D3D11 描画コマンド群を GPU タイムラインとして記録する
    TracyD3D11Zone(m_gfxDevice.GetTracyContext(), "Graphics::Render Pipeline (GPU)");

    m_pipeline.Execute(ctx);
    UnbindAllResources(ctx.dc);
}

// ============================================================
//  MakeFrameContext
// ============================================================
RenderFrameContext Graphics::MakeFrameContext(ID3D11RenderTargetView* overrideRTV) noexcept
{
    RenderFrameContext ctx;
    ctx.dc = m_gfxDevice.GetContext();
    ctx.camera = &m_mainCamera; // ← 直接参照
    ctx.targetRTV = (overrideRTV != nullptr) ? overrideRTV : m_gfxDevice.GetGameRTV();
    ctx.targetDSV = m_gfxDevice.GetDepthStencilView();
    ctx.sceneDepthSRV = m_gfxDevice.GetDepthSRV();
    ctx.sceneNormalSRV = m_sceneNormalSRV;
    ctx.skyboxSRV = m_pendingSkyboxSRV; // EnvironmentSystem が解決した現在の空
    ctx.viewportW = m_gfxDevice.GetWidthU();
    ctx.viewportH = m_gfxDevice.GetHeightU();
    return ctx;
}

// ============================================================
//  UnbindAllResources（パイプライン実行後のクリーンアップ）
// ============================================================
void Graphics::UnbindAllResources(ID3D11DeviceContext* dc)
{
    ID3D11Buffer* nullCB = nullptr;
    ID3D11SamplerState* nullSampler = nullptr;
    ID3D11ShaderResourceView* nullSRV = nullptr;

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

// ============================================================
//  Shutdown
// ============================================================
void Graphics::Shutdown()
{
    // ==============================================================
    // 1. マネージャー群（シングルトン）が握っているGPUリソースを強制解放
    // （絶対にデバイスを破壊する「前」に行うこと！）
    // ==============================================================
    ResourceManager::Instance().ClearAll();
    ShaderRegistry::Instance().ClearAll();

    // ==============================================================
    // 2. パイプライン・パスの破棄
    // ==============================================================
    if (m_gfxDevice.GetContext()) {
        UnbindAllResources(m_gfxDevice.GetContext());
    }

    // パイプラインを解放（各パスの Shutdown が走る）
    m_pipeline.Shutdown();


    // 描画サブシステムを生成の逆順に解放
    m_postProcess.reset();
    m_lightManager.reset();
    m_particleRenderer.reset();
    m_shadowMap.reset();
    m_shapeRenderer.reset();
    m_primitiveRenderer.reset();
    m_modelRenderer.reset();
    m_renderState.reset();

    // D3D11 RHI 層を最後に解放
    m_gfxDevice.Shutdown();
}



// ============================================================
//  Present（デバイスロスト検出付き）
// ============================================================
/**
 * @brief スワップチェーンを提示する。
 *        DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET を受け取ったら
 *        m_deviceLost フラグをセットして false を返す。
 *        呼び出し元は IsDeviceLost() を確認し RecoverFromDeviceLost() を呼ぶこと。
 */
bool Graphics::Present(UINT syncInterval)
{
    HRESULT hr = m_gfxDevice.Present(syncInterval);

    // ウィンドウが完全に隠れている状態: 描画はスキップされるが正常
    if (hr == DXGI_STATUS_OCCLUDED) return true;

    // ===== デバイスロスト検出 =====
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        // 実際の原因コードをログに残す
        const HRESULT reason = m_gfxDevice.GetDeviceRemovedReason();
        char buf[128];
        sprintf_s(buf,
            "[Graphics] !!! Device lost detected. Reason=0x%08X "
            "(HUNG=0x887A0006 RESET=0x887A0007 REMOVED=0x887A0005 DRIVER=0x887A0020)\n",
            static_cast<unsigned int>(reason));
        OutputDebugStringA(buf);

        m_deviceLost = true;
        return false;
    }

    _ASSERT_EXPR(SUCCEEDED(hr), HRTrace(hr));
    return true;
}

// ============================================================
//  RecoverFromDeviceLost
// ============================================================
/**
 * @brief デバイスロストからの完全復帰。
 *
 * @details 復帰の流れ:
 *  1. HWND を保存（Shutdown 後も m_gfxDevice.m_hWnd は生きている）
 *  2. Shutdown()   → 全 D3D リソースを解放
 *  3. ShaderRegistry・ResourceManager のキャッシュをクリア
 *  4. Initialize() → 新しいデバイスで全リソースを再生成
 *
 * @note 復帰後、ゲームシーンが持つテクスチャ・メッシュは再ロードが必要です。
 *       BaseScene::OnDeviceRecreated() 等を追加してシーン側に通知してください。
 *
 * @return 復帰成功なら true。失敗した場合はアプリを終了してください。
 */
bool Graphics::RecoverFromDeviceLost()
{

    OutputDebugStringA("[Graphics] ===== Device Lost Recovery: START =====\n");

    // ---- Step 1: HWND の保存 ----
    // Shutdown() を呼んでも GraphicsDevice.m_hWnd はリセットされないため安全
    HWND hWnd = m_gfxDevice.GetWindowHandle();
    if (!hWnd)
    {
        OutputDebugStringA("[Graphics] RecoverFromDeviceLost: HWND が無効です。復帰失敗。\n");
        return false;
    }

    // ---- Step 2: 全 D3D リソース解放 ----
    // Shutdown() は以下をまとめて行う:
    //   - ResourceManager::ClearAll()   テクスチャ・メッシュなど全 GPU リソース
    //   - ShaderRegistry::ClearAll()    シェーダーキャッシュとファクトリ
    //   - m_pipeline → サブシステム → m_gfxDevice の順で解放
    Shutdown();
    m_deviceLost = false;

    // ---- Step 3: 新しいデバイスで全リソースを再生成 ----
    // Initialize() は以下を行う:
    //   - GraphicsDevice → 新しい D3D11 デバイス・スワップチェーンを生成
    //   - RegisterMaterialShader / RegisterShader → ファクトリを再登録
    //   - SetupRenderPipeline() → パイプライン・パスを再構築
    // シェーダーインスタンスは GetShader() が初回呼び出し時に.csoからロードし直す
    Initialize(hWnd);

    OutputDebugStringA("[Graphics] ===== Device Lost Recovery: COMPLETE =====\n");
    return true;
}
