#include "Graphics.h"
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
    _gfxDevice.Initialize(hWnd);
    ID3D11Device* device = _gfxDevice.GetDevice();

    // Camera::Camera() がデフォルト値で初期化済み。
    // アスペクト比だけ実際のウィンドウサイズで上書きする（BaseScene が後でゲーム固有値を設定する）。
    float aspect = _gfxDevice.GetWidth() / _gfxDevice.GetHeight();
    _mainCamera.SetPerspectiveFov(DirectX::XMConvertToRadians(45.0f), aspect, 0.1f, 1000.0f);

    // ---- Phase 2: 描画サブシステム生成 ----
    _renderState = std::make_unique<RenderState>(device);
    _primitiveRenderer = std::make_unique<PrimitiveRenderer>(device);
    _shapeRenderer = std::make_unique<ShapeRenderer>(device);
    _modelRenderer = std::make_unique<ModelRenderer>(device);
    _shadowMap = std::make_unique<ShadowMap>(device, 2048, 2048);
    _particleRenderer = std::make_unique<ParticleRenderer>();
    _particleRenderer->Initialize(device);
    _lightManager = std::make_unique<LightManager>();
    _lightManager->Initialize(device);
    _postProcess = std::make_unique<PostProcessManager>();
    _postProcess->Initialize(device, _gfxDevice.GetWidthU(), _gfxDevice.GetHeightU());

    // ModelRenderer に CbScene ポインタを渡す（BindAllGlobalConstantBuffers 用）
    _modelRenderer->SetSceneConstantBuffer(_gfxDevice.GetSceneConstantBuffer());

    // ---- Phase 3: シェーダー登録 ----
    auto& reg = ShaderRegistry::Instance();

    reg.RegisterShader<ErrorShader>("Error"_hash);  

    reg.RegisterMaterialShader("PBR"_hash,
        "Assets/Shader/PBR_VS.cso", L"Shader/Material/PBR/PBR_VS.hlsl",
        "Assets/Shader/PBR_PS.cso", L"Shader/Material/PBR/PBR_PS.hlsl");
    reg.RegisterMaterialShader("Phong"_hash,
        "Assets/Shader/PhongVS.cso", L"Shader/Material/Phong/PhongVS.hlsl",
        "Assets/Shader/PhongPS.cso", L"Shader/Material/Phong/PhongPS.hlsl");
    reg.RegisterMaterialShader("Lambert"_hash,
        "Assets/Shader/LambertVS.cso", L"Shader/Material/Lambert/LambertVS.hlsl",
        "Assets/Shader/LambertPS.cso", L"Shader/Material/Lambert/LambertPS.hlsl");
    reg.RegisterMaterialShader("Basic"_hash,
        "Assets/Shader/BasicVS.cso", L"Shader/Material/Basic/BasicVS.hlsl",
        "Assets/Shader/BasicPS.cso", L"Shader/Material/Basic/BasicPS.hlsl");
    reg.RegisterMaterialShader("Toon"_hash,
        "Assets/Shader/ToonVS.cso", L"Shader/Material/Toon/ToonVS.hlsl",
        "Assets/Shader/ToonPS.cso", L"Shader/Material/Toon/ToonPS.hlsl");
    reg.RegisterShader<OutlineShader>("Outline"_hash);
    reg.RegisterShader<ShadowMapShader>("ShadowMap"_hash);

    // Graphics.cpp の Phase 3 に追加
    reg.RegisterShader<ParticleShader>("Particle"_hash);

    // ---- Phase 4: レンダリングパイプライン構築 ----
    SetupRenderPipeline();
}

void Graphics::SetupRenderPipeline()
{
    // RenderServices を構築してキャッシュ
    _renderServices.device = _gfxDevice.GetDevice();
    _renderServices.sceneConstantBuffer = _gfxDevice.GetSceneConstantBuffer();
    _renderServices.renderState = _renderState.get();
    _renderServices.resources = &ResourceManager::Instance();
    _renderServices.shaders = &ShaderRegistry::Instance();
    _renderServices.modelRenderer = _modelRenderer.get();
    _renderServices.primitiveRenderer = _primitiveRenderer.get();
    _renderServices.shapeRenderer = _shapeRenderer.get();
    _renderServices.particleRenderer = _particleRenderer.get();
    _renderServices.shadowMap = _shadowMap.get();
    _renderServices.renderQueue = &RenderQueue::Instance();
    _renderServices.lightManager = _lightManager.get();
    _renderServices.postProcess = _postProcess.get();

    // (I) RenderQueue に ShaderRegistry を注入（ホットパス singleton ゼロ）
    RenderQueue::Instance().Initialize(_renderServices.shaders);

    // パイプライン初期化（AddPass の前に必ず呼ぶ）
    _pipeline.Initialize(_renderServices);

    // レンダーパスの構築
	_pipeline.BuildDefaultPipeline();
   
    OutputDebugStringA("[Graphics] RenderPipeline initialized.\n");
}

// ============================================================
//  Update（毎フレーム）
//  ★ CbScene の書き込み権限はここのみ。EnvironmentSystem は上書きしない。
// ============================================================
void Graphics::Update()
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    ShaderRegistry::Instance().UpdateHotReload(_gfxDevice.GetDevice());
#endif

    // CbLight (b9) を GPU に転送・バインド（毎フレーム最初に実行）
    if (_lightManager) {
        _lightManager->Bind(_gfxDevice.GetContext(), SLOT_CB_LIGHT);
    }

    if (!_gfxDevice.GetSceneConstantBuffer()) return;

    // ★ CbScene を構築（唯一の書き込み箇所）
    GpuData::CbScene data{};

    // XMFLOAT4X4 は row_major ストレージ = HLSL の row_major float4x4 と一致
    data.viewProjection = _mainCamera.GetViewProjectionMatrix();
    data.gView          = _mainCamera.GetView();
    data.gProjection    = _mainCamera.GetProjection();

    const DirectX::XMFLOAT3& pos = _mainCamera.GetEye();
    data.cameraPosition = { pos.x, pos.y, pos.z, 1.0f };

    if (_lightManager) {
        const DirectionalLight& dir = _lightManager->GetDirectionalLight();
        data.lightDirection = { dir.direction.x, dir.direction.y, dir.direction.z, 0.0f };
        data.lightColor = { dir.color.x,     dir.color.y,     dir.color.z, dir.intensity };
    }

    _gfxDevice.UpdateSceneConstantBuffer(&data, sizeof(data));
}

// ============================================================
//  Render（毎フレーム） ← 旧 ModelRenderer::Render
// ============================================================
void Graphics::Render(const RenderFrameContext& ctx)
{
    // このスコープ内の D3D11 描画コマンド群を GPU タイムラインとして記録する
    TracyD3D11Zone(_gfxDevice.GetTracyContext(), "Graphics::Render Pipeline (GPU)");

    _pipeline.Execute(ctx);
    UnbindAllResources(ctx.dc);
}

// ============================================================
//  MakeFrameContext
// ============================================================
RenderFrameContext Graphics::MakeFrameContext(ID3D11RenderTargetView* overrideRTV) noexcept
{
    RenderFrameContext ctx;
    ctx.dc = _gfxDevice.GetContext();
    ctx.camera = &_mainCamera; // ← 直接参照
    ctx.targetRTV = (overrideRTV != nullptr) ? overrideRTV : _gfxDevice.GetGameRTV();
    ctx.targetDSV = _gfxDevice.GetDepthStencilView();
    ctx.viewportW = _gfxDevice.GetWidthU();
    ctx.viewportH = _gfxDevice.GetHeightU();
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
    if (_gfxDevice.GetContext()) {
        UnbindAllResources(_gfxDevice.GetContext());
    }

    // パイプラインを解放（各パスの Shutdown が走る）
    _pipeline.Shutdown();


    // 描画サブシステムを生成の逆順に解放
    _postProcess.reset();
    _lightManager.reset();
    _particleRenderer.reset();
    _shadowMap.reset();
    _shapeRenderer.reset();
    _primitiveRenderer.reset();
    _modelRenderer.reset();
    _renderState.reset();

    // D3D11 RHI 層を最後に解放
    _gfxDevice.Shutdown();
}



// ============================================================
//  Present（デバイスロスト検出付き）
// ============================================================
/**
 * @brief スワップチェーンを提示する。
 *        DXGI_ERROR_DEVICE_REMOVED / DEVICE_RESET を受け取ったら
 *        _deviceLost フラグをセットして false を返す。
 *        呼び出し元は IsDeviceLost() を確認し RecoverFromDeviceLost() を呼ぶこと。
 */
bool Graphics::Present(UINT syncInterval)
{
    HRESULT hr = _gfxDevice.Present(syncInterval);

    // ウィンドウが完全に隠れている状態: 描画はスキップされるが正常
    if (hr == DXGI_STATUS_OCCLUDED) return true;

    // ===== デバイスロスト検出 =====
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        // 実際の原因コードをログに残す
        const HRESULT reason = _gfxDevice.GetDeviceRemovedReason();
        char buf[128];
        sprintf_s(buf,
            "[Graphics] !!! Device lost detected. Reason=0x%08X "
            "(HUNG=0x887A0006 RESET=0x887A0007 REMOVED=0x887A0005 DRIVER=0x887A0020)\n",
            static_cast<unsigned int>(reason));
        OutputDebugStringA(buf);

        _deviceLost = true;
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
 *  1. HWND を保存（Shutdown 後も _gfxDevice._hWnd は生きている）
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
    // Shutdown() を呼んでも GraphicsDevice._hWnd はリセットされないため安全
    HWND hWnd = _gfxDevice.GetWindowHandle();
    if (!hWnd)
    {
        OutputDebugStringA("[Graphics] RecoverFromDeviceLost: HWND が無効です。復帰失敗。\n");
        return false;
    }

    // ---- Step 2: 全 D3D リソース解放 ----
    // Shutdown() は以下をまとめて行う:
    //   - ResourceManager::ClearAll()   テクスチャ・メッシュなど全 GPU リソース
    //   - ShaderRegistry::ClearAll()    シェーダーキャッシュとファクトリ
    //   - _pipeline → サブシステム → _gfxDevice の順で解放
    Shutdown();
    _deviceLost = false;

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
