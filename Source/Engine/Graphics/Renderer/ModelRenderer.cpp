#include "ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"

// SetupPipeline で AddPass する全パス（登録順 = 実行順）
#include "Engine/Graphics/Pipeline/SetupPass.h"
#include "Engine/Graphics/Pipeline/ShadowPass.h"
#include "Engine/Graphics/Pipeline/OpaquePass.h"
#include "Engine/Graphics/Pipeline/SkyboxPass.h"
#include "Engine/Graphics/Pipeline/TransparentPass.h"
#include "Engine/Graphics/Pipeline/ParticlePass.h"
#include "Engine/Graphics/Pipeline/PrimitivePass.h"
#include "Engine/Graphics/Pipeline/PostProcessPass.h"

// ============================================================
//  Phase 1 ― 軽量な構築（スケルトン定数バッファのみ）
// ============================================================

ModelRenderer::ModelRenderer(ID3D11Device* device)
{
    InitializeGlobalConstantBuffers(device);
    // パイプラインは SetupPipeline まで未構築。
    // Graphics 側で全サブシステムを生成してから SetupPipeline を呼ぶこと。
}

// ============================================================
//  デストラクタ
// ============================================================

ModelRenderer::~ModelRenderer()
{
    // パスを追加と逆順で Shutdown（依存解放の順序保証）
    _pipeline.Shutdown();
    // _cbSkeletonBuffer は ComPtr のため、ここで自動解放される。
}

// ============================================================
//  Phase 2 ― パイプライン組み立て（全サブシステム確保後に 1 回だけ呼ぶ）
// ============================================================

void ModelRenderer::SetupPipeline(const RenderServices& services)
{
    // ★ 必ずここで Initialize してから AddPass すること。
    //   AddPass の内部は pass->Initialize(_services) を即呼ぶため、
    //   Initialize より前に AddPass すると _services が空のまま渡ってクラッシュする。
    _pipeline.Initialize(services);

    // CbScene バッファを services から受け取りキャッシュする。
    // BindAllGlobalConstantBuffers で使用する（Graphics::Instance() 不要）。
    _sceneCB = services.sceneConstantBuffer;

    // ---- 描画パスを実行順に登録 ----
    _pipeline.AddPass<SetupPass>();       // 1. HDR バッファのクリアとバインド
    _pipeline.AddPass<ShadowPass>();      // 2. 影（ライト視点の深度書き込み）
    _pipeline.AddPass<OpaquePass>();      // 3. 不透明（Z バッファ + 色の書き込み）
    _pipeline.AddPass<SkyboxPass>();      // 4. スカイボックス（Z=1.0 の隙間を埋める）
    _pipeline.AddPass<TransparentPass>(); // 5. 半透明（奥から Z テスト、深度書き込みなし）
    _pipeline.AddPass<ParticlePass>();    // 6. パーティクル（半透明の上）
    _pipeline.AddPass<PrimitivePass>();   // 7. プリミティブ / デバッグ線（ワールド末尾）
    _pipeline.AddPass<PostProcessPass>(); // 8. ポストプロセス（HDR→SDR、最終出力）

    OutputDebugStringA("[ModelRenderer] Pipeline initialized.\n");
}

// ============================================================
//  Render ― 新 API（RenderFrameContext 版）
// ============================================================

void ModelRenderer::Render(const RenderFrameContext& ctx)
{
    _pipeline.Execute(ctx);
    UnbindAllResources(ctx.dc);
}


// ============================================================
//  内部初期化
// ============================================================

void ModelRenderer::InitializeGlobalConstantBuffers(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(CbSkeleton);
    if (desc.ByteWidth % 16 != 0)
        desc.ByteWidth = ((desc.ByteWidth + 15) / 16) * 16;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    HRESULT hr = device->CreateBuffer(&desc, nullptr, _cbSkeletonBuffer.GetAddressOf());
    if (FAILED(hr)) {
        OutputDebugStringA("[ModelRenderer] CRITICAL: CbSkeleton buffer creation failed.\n");
    }
}

// ============================================================
//  UpdateSkeletonConstants
// ============================================================

void ModelRenderer::UpdateSkeletonConstants(ID3D11DeviceContext* dc, const Model::Mesh& mesh)
{
    if (!_cbSkeletonBuffer) return;

    if (!mesh.bones.empty()) {
        const size_t count = (std::min)(mesh.bones.size(), static_cast<size_t>(MAX_BONES));
        for (size_t i = 0; i < count; ++i) {
            const Model::Bone& bone = mesh.bones[i];
            const DirectX::XMMATRIX matOffset = DirectX::XMLoadFloat4x4(&bone.data->offsetTransform);
            const DirectX::XMMATRIX matWorld = DirectX::XMLoadFloat4x4(&bone.node->worldTransform);
            DirectX::XMStoreFloat4x4(&_cbSkeletonCache.boneTransforms[i], matOffset * matWorld);
        }
    }
    else {
        // ボーンなし：ノードのワールド変換を [0] に格納する
        if (mesh.node) {
            _cbSkeletonCache.boneTransforms[0] = mesh.node->worldTransform;
        }
    }

    dc->UpdateSubresource(_cbSkeletonBuffer.Get(), 0, nullptr, &_cbSkeletonCache, 0, 0);

    ID3D11Buffer* buffer = _cbSkeletonBuffer.Get();
    dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &buffer);
}

// ============================================================
//  BindAllGlobalConstantBuffers
//  NOTE: CbScene の取得に Graphics::Instance() を使用中（残存シングルトン）。
//        CbScene を RenderServices / RenderFrameContext に持たせたら除去する。
// ============================================================

void ModelRenderer::BindAllGlobalConstantBuffers(ID3D11DeviceContext* dc)
{
    // _sceneCB は SetupPipeline で services.sceneConstantBuffer から受け取る。
    if (_sceneCB) {
        dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &_sceneCB);
        dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &_sceneCB);
    }

    if (_cbSkeletonBuffer) {
        ID3D11Buffer* skelBuf = _cbSkeletonBuffer.Get();
        dc->VSSetConstantBuffers(SLOT_CB_SKELETON, 1, &skelBuf);
    }
}

// ============================================================
//  UnbindAllResources
// ============================================================

void ModelRenderer::UnbindAllResources(ID3D11DeviceContext* dc)
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