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
    // MakeLegacyContext 不要: _services / ctx を直接参照
    ID3D11DeviceContext* dc = ctx.dc;
    RenderQueue* queue = _services->renderQueue;
    ModelRenderer* renderer = _services->modelRenderer;
    if (!queue || !renderer) return;

    if (_services->shadowMap) {
        _services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }
    _psoTransparent.Apply(dc, _services->renderState);

    auto allTrans = queue->GetDeferredTransparentCommands();
    for (const auto& t : queue->GetTransparentCommands()) allTrans.push_back(t);

    std::sort(allTrans.begin(), allTrans.end(),
        [](const TransparencyCommand& l, const TransparencyCommand& r) {
            return l.distance > r.distance;
        });

    for (const TransparencyCommand& info : allTrans) {
        Shader* shader = _services->shaders->GetShader(info.shaderHash, _services->device);
        if (!shader) continue;

        shader->Begin(dc, *_services, ctx);

        const Model::Mesh& mesh = *info.mesh;
        Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
        Model::Material* origMat = mutableMesh.material;
        Model::Material  tempWrapper;

        bool applyOverride = info.overrideMaterial.IsValid();
        if (applyOverride) {
            MaterialData* ptr = _services->resources->GetMaterial(info.overrideMaterial);
            if (ptr) {
                tempWrapper.data = std::shared_ptr<MaterialData>(ptr, [](MaterialData*) {});
                tempWrapper.name = "TranspOverride";
                mutableMesh.material = &tempWrapper;
            }
            else { applyOverride = false; }
        }

        const bool isSkinned = !mesh.bones.empty();
        InstanceData singleData;
        singleData.customParams = info.customParams;
        if (isSkinned) {
            DirectX::XMStoreFloat4x4(&singleData.world, DirectX::XMMatrixIdentity());
        }
        else {
            singleData.world = info.worldMatrix;
        }

        std::vector<InstanceData> temp = { singleData };
        _instanceBuffer.Write(dc, temp);
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        if (isSkinned) renderer->UpdateSkeletonConstants(dc, mesh);

        shader->Update(dc, mesh);

        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        if (applyOverride) mutableMesh.material = origMat;
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(dc, *_services);
    }
}