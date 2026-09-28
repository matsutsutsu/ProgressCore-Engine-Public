#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/RHI/Light.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/RenderDebugRegistry.h"
#include "Engine/Graphics/Shader/Pass/ShadowUtils.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void ShadowPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoShadow = PipelineState(PipelineStateDesc::DefaultShadow());
    m_instanceBuffer.Create(services.device, 8192);
}

void ShadowPass::Execute(const RenderFrameContext &ctx)
{
    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    const auto &shadowCommands = queue->GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext *dc        = ctx.dc;
    auto                &shadowMap = m_services->shadowMap;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT           numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    // シェーダーの取得・カスケードの行列・メッシュの一覧と並べ替えは、どのカスケードでも
    // 同じなので、カスケードのループの外で1回だけ行う
    Shader *shader = m_services->shaders->GetShader("ShadowMap"_hash, m_services->device);
    if (!shader) return;

    const DirectX::XMFLOAT3 lightDir =
        m_services->lightManager->GetDirectionalLight().direction;

    DirectX::XMMATRIX lightVPs[3];
    CalculateCascadeMatrices(
        XMLoadFloat4x4(&ctx.camera->GetView()),
        ctx.camera->GetFovY(),
        ctx.camera->GetAspect(),
        lightDir,
        shadowMap->params.cascadeSplits,
        lightVPs);

    // 影を落とすメッシュを集め、同じメッシュが並ぶように並べる。
    // cascadeMask での絞り込みは、カスケードごとにバッチを作るときに行う
    m_itemsScratch.clear();
    m_itemsScratch.reserve(shadowCommands.size());
    for (const auto &cmd : shadowCommands)
    {
        if (!cmd.mesh) continue;
        SortItem item;
        item.mesh        = cmd.mesh;
        item.cascadeMask = cmd.cascadeMask;
        if (!cmd.mesh->bones.empty() || !cmd.mesh->node)
        {
            item.worldMatrix = cmd.worldMatrix;
        }
        else
        {
            const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform);
            const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
            DirectX::XMStoreFloat4x4(&item.worldMatrix, ml * ew);
        }
        m_itemsScratch.push_back(item);
    }
    std::sort(m_itemsScratch.begin(), m_itemsScratch.end(),
              [](const SortItem &a, const SortItem &b) { return a.mesh < b.mesh; });

    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex)
    {
        shadowMap->Activate(dc, cascadeIndex);
        m_psoShadow.Apply(dc, m_services->renderState);

        GpuData::CbShadow cbData{};
        XMStoreFloat4x4(&cbData.light_view_projection[0], lightVPs[0]);
        XMStoreFloat4x4(&cbData.light_view_projection[1], lightVPs[1]);
        XMStoreFloat4x4(&cbData.light_view_projection[2], lightVPs[2]);
        cbData.cascadeSplits = {shadowMap->params.cascadeSplits[0],
                                shadowMap->params.cascadeSplits[1],
                                shadowMap->params.cascadeSplits[2], 0.0f};

        cbData.shadow_color        = shadowMap->params.shadowColor;
        cbData.shadow_attenuation  = shadowMap->params.shadowAttenuation;
        cbData.shadow_bias         = shadowMap->params.shadowBias;
        cbData.currentCascadeIndex = cascadeIndex;
        shadowMap->UpdateConstantBuffer(dc, cbData);
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);

        shader->Begin(dc, *m_services, ctx);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const Model::Mesh *lastBound = nullptr; // 今 IA に設定しているメッシュ（スキンメッシュ用）

        // Batch は m_batchesScratch から使い回す（Batch::data の確保済みの容量も残る）
        m_batchCount      = 0;
        auto acquireBatch = [&]() -> Batch & {
            if (m_batchCount >= m_batchesScratch.size())
                m_batchesScratch.emplace_back();
            Batch &b = m_batchesScratch[m_batchCount++];
            b.mesh   = nullptr;
            b.data.clear();
            return b;
        };

        for (const auto &info : m_itemsScratch)
        {
            if (!info.mesh || !info.mesh->data) continue;
            if ((info.cascadeMask & (1 << cascadeIndex)) == 0) continue;

            const bool isSkinned = !info.mesh->bones.empty();
            if (isSkinned)
            {
                modelConstants->UpdateSkeletonConstants(dc, *info.mesh);
                if (lastBound != info.mesh)
                {
                    UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                    dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

                    lastBound = info.mesh;
                }
                dc->DrawIndexed(static_cast<UINT>(info.mesh->data->indices.size()), 0, 0);
            }
            else
            {
                if (m_batchCount > 0 && m_batchesScratch[m_batchCount - 1].mesh == info.mesh)
                {
                    m_batchesScratch[m_batchCount - 1].data.push_back({info.worldMatrix});
                }
                else
                {
                    Batch &b = acquireBatch();
                    b.mesh   = info.mesh;
                    b.data.push_back({info.worldMatrix});
                }
            }
        }

        // インスタンスの SRV はどのバッチでも同じなので、バインドはループの外で1回だけ
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        for (size_t bi = 0; bi < m_batchCount; ++bi)
        {
            const Batch &batch = m_batchesScratch[bi];

            UINT stride = sizeof(ModelResource::Vertex), zero = 0;
            dc->IASetVertexBuffers(0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            size_t total = batch.data.size(), offset = 0;
            while (offset < total)
            {
                const size_t n = (std::min)(total - offset, m_instanceBuffer.GetMaxInstances());
                m_instanceBuffer.Write(dc, batch.data.data() + offset, n);
                dc->DrawIndexedInstanced(
                    static_cast<UINT>(batch.mesh->data->indices.size()),
                    static_cast<UINT>(n), 0, 0, 0);
                offset += n;
            }
        }

        shader->End(dc, *m_services);
        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // デバッグ表示に登録する（Texture2DArray なので、表示するスライスは RenderDebugWindow で選ぶ）
    RenderDebugRegistry::Instance().Register({"ShadowMap (Cascades)", shadowMap->GetShaderResourceView(),
                                              shadowMap->GetWidth(), shadowMap->GetHeight(), DebugTexKind::Array});
}