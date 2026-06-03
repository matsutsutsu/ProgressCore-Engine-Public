#include "Graphics.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/Camera.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/ShaderShared.h"

#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"
#include "Engine/Graphics/Shader/Material/ParticleShader.h"

// パス登録用
#include "Engine/Graphics/Pipeline/SetupPass.h"
#include "Engine/Graphics/Pipeline/ShadowPass.h"
#include "Engine/Graphics/Pipeline/OpaquePass.h"
#include "Engine/Graphics/Pipeline/SkyboxPass.h"
#include "Engine/Graphics/Pipeline/TransparentPass.h"
#include "Engine/Graphics/Pipeline/ParticlePass.h"
#include "Engine/Graphics/Pipeline/PrimitivePass.h"
#include "Engine/Graphics/Pipeline/PostProcessPass.h"

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

    // 描画パスを実行順に登録
    _pipeline.AddPass<SetupPass>();       // 1. HDR バッファのクリアとバインド
    _pipeline.AddPass<ShadowPass>();      // 2. シャドウマップ生成
    _pipeline.AddPass<OpaquePass>();      // 3. 不透明ジオメトリ
    _pipeline.AddPass<SkyboxPass>();      // 4. スカイボックス
    _pipeline.AddPass<TransparentPass>(); // 5. 半透明
    _pipeline.AddPass<ParticlePass>();    // 6. パーティクル
    _pipeline.AddPass<PrimitivePass>();   // 7. デバッグプリミティブ
    _pipeline.AddPass<PostProcessPass>(); // 8. ポストプロセス・最終出力

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