#include "Graphics.h"
#include "Engine/Core/Math/Misc.h"
#include "Engine/Graphics/RHI/Camera.h"

#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

#include "Engine/Graphics/Shader/Material/OutlineShader.h"
#include "Engine/Graphics/Shader/Material/ShadowMapShader.h"

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
//  CbScene 構造体 (HLSL: CbScene b8)
//  alignas(16) + XMMATRIX を含む → 240 バイト
//  ★ 唯一の権威あるソース。EnvironmentSystem は上書きしない。
// ============================================================
struct alignas(16) CbSceneData
{
    DirectX::XMMATRIX viewProjection; // offset:   0, size: 64
    DirectX::XMFLOAT4 lightDirection; // offset:  64, size: 16
    DirectX::XMFLOAT4 lightColor;     // offset:  80, size: 16
    DirectX::XMFLOAT4 cameraPosition; // offset:  96, size: 16
    DirectX::XMMATRIX view;           // offset: 112, size: 64
    DirectX::XMMATRIX projection;     // offset: 176, size: 64
};
static_assert(sizeof(CbSceneData) == 240,
    "CbSceneData size mismatch — GraphicsDevice.cpp の ByteWidth=240 と一致させること");

// ============================================================
//  Initialize
// ============================================================
void Graphics::Initialize(HWND hWnd)
{
    // ---- Phase 1: D3D11 RHI 層 ----
    _gfxDevice.Initialize(hWnd);
    ID3D11Device* device = _gfxDevice.GetDevice();

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
void Graphics::Update(Camera* camera)
{
#if defined(_DEBUG) || defined(ENABLE_EDITOR)
    ShaderRegistry::Instance().UpdateHotReload(_gfxDevice.GetDevice());
#endif

    // CbLight (b9) を GPU に転送・バインド（毎フレーム最初に実行）
    if (_lightManager) {
        _lightManager->Bind(_gfxDevice.GetContext(), SLOT_CB_LIGHT);
    }

    if (!_gfxDevice.GetSceneConstantBuffer() || !camera) return;

    // ★ CbScene を構築（唯一の書き込み箇所）
    CbSceneData data{};

    const DirectX::XMFLOAT4X4 vp = camera->GetViewProjectionMatrix();
    data.viewProjection = DirectX::XMLoadFloat4x4(&vp);
    data.view = DirectX::XMLoadFloat4x4(&camera->GetView());
    data.projection = DirectX::XMLoadFloat4x4(&camera->GetProjection());

    const auto& pos = camera->GetEye();
    data.cameraPosition = { pos.x, pos.y, pos.z, 1.0f };

    if (_lightManager) {
        const DirectionalLight& dir = _lightManager->GetDirectionalLight();
        data.lightDirection = { dir.direction.x, dir.direction.y, dir.direction.z, 0.0f };
        data.lightColor = { dir.color.x, dir.color.y, dir.color.z, dir.intensity };
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
RenderFrameContext Graphics::MakeFrameContext(const Camera* camera,
    ID3D11RenderTargetView* overrideRTV) noexcept
{
    RenderFrameContext ctx;
    ctx.dc = _gfxDevice.GetContext();
    ctx.camera = camera;
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
    // パイプラインを最初に解放（各パスの Shutdown が走る）
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