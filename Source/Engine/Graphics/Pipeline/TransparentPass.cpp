#include "TransparentPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void TransparentPass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);
    _psoTransparent = PipelineState(PipelineStateDesc::DefaultTransparent());
    _instanceBuffer.Create(services.device, 8192);
}

void TransparentPass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Transparent");
#endif

    RenderContext rc = MakeLegacyContext(*_services, ctx);

    RenderQueue* queue = rc.renderQueue;
    ModelRenderer* renderer = rc.modelRenderer;
    if (!queue || !renderer) return;

    ID3D11DeviceContext* dc = rc.deviceContext;

    if (rc.shadowMap) {
        rc.shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    _psoTransparent.Apply(dc, rc.renderState);

    // ECS 直通分 + OpaquePass から転送された半透明コマンドを合算
    auto allTrans = queue->GetDeferredTransparentCommands();
    for (const auto& t : queue->GetTransparentCommands()) {
        allTrans.push_back(t);
    }

    // 奥から手前へソート
    std::sort(allTrans.begin(), allTrans.end(),
        [](const TransparencyCommand& lhs, const TransparencyCommand& rhs) {
            return lhs.distance > rhs.distance;
        });

    for (const TransparencyCommand& info : allTrans) {
        // ShaderRegistry::Instance() / Graphics::Instance() → services 経由に変更
        Shader* shader = _services->shaders->GetShader(info.shaderHash, _services->device);
        if (!shader) continue;

        shader->Begin(rc);

        const Model::Mesh& mesh = *info.mesh;
        Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
        Model::Material* originalMat = mutableMesh.material;
        Model::Material    tempWrapper;

        bool applyOverride = info.overrideMaterial.IsValid();
        if (applyOverride) {
            // ResourceManager::Instance() → services 経由に変更
            MaterialData* ptr = _services->resources->GetMaterial(info.overrideMaterial);
            if (ptr) {
                tempWrapper.data = std::shared_ptr<MaterialData>(ptr, [](MaterialData*) {});
                tempWrapper.name = "TranspOverride";
                mutableMesh.material = &tempWrapper;
            }
            else {
                applyOverride = false;
            }
        }

        const bool isSkinned = !mesh.bones.empty();

        InstanceData singleData;
        singleData.customParams = info.customParams;

        if (isSkinned) {
            // スキンド：シェーダーの Skinning() がワールド座標まで完結する
            DirectX::XMStoreFloat4x4(&singleData.world, DirectX::XMMatrixIdentity());
        }
        else {
            // OpaquePass 転送コマンドはノード合成済み、ECS 直通は呼び出し元で合成すること
            singleData.world = info.worldMatrix;
        }

        std::vector<InstanceData> temp = { singleData };
        _instanceBuffer.Write(dc, temp);
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        if (isSkinned) {
            renderer->UpdateSkeletonConstants(dc, mesh);
        }

        shader->Update(rc, mesh);

        UINT stride = sizeof(ModelResource::Vertex);
        UINT offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        if (applyOverride) mutableMesh.material = originalMat;

        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }
}