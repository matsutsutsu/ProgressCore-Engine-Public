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

    ID3D11DeviceContext* dc = ctx.dc;
    RenderQueue* queue = _services->renderQueue;
    ModelRenderer* renderer = _services->modelRenderer;
    if (!queue || !renderer) return;

    if (_services->shadowMap) {
        _services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }
    _psoTransparent.Apply(dc, _services->renderState);

    const PipelineState *lastPso = &_psoTransparent;   

    auto allTrans = queue->GetDeferredTransparentCommands();
    for (const auto& t : queue->GetTransparentCommands()) allTrans.push_back(t);

    std::sort(allTrans.begin(), allTrans.end(),
        [](const TransparencyCommand& l, const TransparencyCommand& r) {
            return l.distance > r.distance;
        });

    for (const TransparencyCommand& info : allTrans) {
        Shader* shader = _services->shaders->GetShader(info.shaderHash, _services->device);
        IMaterialShader* matShader = shader ? shader->AsMaterial() : nullptr;

        if (!matShader) continue;
        matShader->Begin(dc, *_services, ctx);

        // シェーダー固有 PSO があれば上書き。無ければパス既定へ戻す。
        {
            const PipelineState* want = shader->GetPSO() ? shader->GetPSO() : &_psoTransparent;
            if (want != lastPso) { want->Apply(dc, _services->renderState); lastPso = want; }
        }

        const Model::Mesh& mesh = *info.mesh;

        // =========================================================
        // ★ F: const_cast 廃止
        //    override は引数として取得し、mesh.material を書き換えない。
        // =========================================================
        MaterialData* overrideMat = nullptr;
        if (info.overrideMaterial.IsValid()) {
            overrideMat = _services->resources->GetMaterial(info.overrideMaterial);
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

        // override を明示的に渡す（mesh.material への書き込みゼロ）
        matShader->Update(dc, mesh, overrideMat);

        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        // ★ const_cast による restore 行 完全削除
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        matShader->End(dc, *_services);
    }
}