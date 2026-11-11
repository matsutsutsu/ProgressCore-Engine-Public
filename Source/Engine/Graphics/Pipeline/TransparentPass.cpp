#include "TransparentPass.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void TransparentPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoTransparent = PipelineState(PipelineStateDesc::DefaultTransparent());
    m_instanceBuffer.Create(services.device, 8192);
}



void TransparentPass::Execute(const RenderFrameContext &ctx)
{
    ID3D11DeviceContext  *dc             = ctx.dc;
    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    if (m_services->shadowMap)
    {
        m_services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }
    // ブレンドあり・深度はテストだけで書かない。ここに来るのは alphaMode が Blend のメッシュだけなので、全部この PSO で描く
    m_psoTransparent.Apply(dc, m_services->renderState);

    // OpaquePass が回した半透明のコマンドを、遠い順にその場で並べる
    // この配列は毎フレーム作り直すので、並べ替えて壊してかまわない
    auto &allTrans = queue->GetDeferredTransparentCommandsMutable();
    std::sort(allTrans.begin(), allTrans.end(),
              [](const TransparencyCommand &l, const TransparencyCommand &r) {
                  return l.distance > r.distance;
              });

    for (const TransparencyCommand &info : allTrans)
    {
        Shader          *shader    = m_services->shaders->GetShader(info.shaderHash, m_services->device);
        IMaterialShader *matShader = shader ? shader->AsMaterial() : nullptr;

        if (!matShader) continue;
        matShader->Begin(dc, *m_services, ctx);

        const Model::Mesh &mesh = *info.mesh;

        // 上書きのマテリアルは引数で渡す。mesh.material に書くと、上書きをやめた後のフレームにも残るので書き換えない
        MaterialData *overrideMat = nullptr;
        if (info.overrideMaterial.IsValid())
        {
            overrideMat = m_services->resources->GetMaterial(info.overrideMaterial);
        }

        const bool   isSkinned = !mesh.bones.empty();
        InstanceData singleData;
        singleData.customParams = info.customParams;
        if (isSkinned)
        {
            DirectX::XMStoreFloat4x4(&singleData.world, DirectX::XMMatrixIdentity());
        }
        else
        {
            singleData.world = info.worldMatrix;
        }

        m_instanceBuffer.Write(dc, &singleData, 1);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        if (isSkinned) modelConstants->UpdateSkeletonConstants(dc, mesh);

        matShader->Update(dc, mesh, overrideMat);

        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        matShader->End(dc, *m_services);
    }
}
