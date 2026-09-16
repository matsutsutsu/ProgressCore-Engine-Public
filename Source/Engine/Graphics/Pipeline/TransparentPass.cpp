#include "TransparentPass.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
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
    m_psoTransparent.Apply(dc, m_services->renderState);

    const PipelineState *lastPso = &m_psoTransparent;

    // OpaquePass が積んだ遅延半透明コマンドを距離降順（奥→手前）でその場ソートする。
    // このリストは毎フレーム作り直されるため破壊的並べ替えで問題なく、コピーも発生しない。
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

        // シェーダー固有 PSO があれば上書き。無ければパス既定へ戻す。
        {
            const PipelineState *want = shader->GetPSO() ? shader->GetPSO() : &m_psoTransparent;
            if (want != lastPso)
            {
                want->Apply(dc, m_services->renderState);
                lastPso = want;
            }
        }

        const Model::Mesh &mesh = *info.mesh;

        // =========================================================
        // F: const_cast 廃止
        //    override は引数として取得し、mesh.material を書き換えない。
        // =========================================================
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

        // 1 体分をポインタ渡しの高速版で書き込む（毎ドローの vector 確保を回避）
        m_instanceBuffer.Write(dc, &singleData, 1);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        if (isSkinned) modelConstants->UpdateSkeletonConstants(dc, mesh);

        // override を明示的に渡す（mesh.material への書き込みゼロ）
        matShader->Update(dc, mesh, overrideMat);

        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        // const_cast による restore 行 完全削除
        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        matShader->End(dc, *m_services);
    }
}
