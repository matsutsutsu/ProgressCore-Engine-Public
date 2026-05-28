#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/RHI/Light.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/Pass/ShadowUtils.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void ShadowPass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);
    _psoShadow = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(services.device, 8192);
}

void ShadowPass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Shadow");
#endif

    RenderContext rc = MakeLegacyContext(*_services, ctx);

    RenderQueue* queue = rc.renderQueue;
    ModelRenderer* renderer = rc.modelRenderer;
    if (!queue || !renderer) return;

    const auto& shadowCommands = queue->GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext* dc = rc.deviceContext;
    auto& shadowMap = rc.shadowMap;

    // 現在のレンダーターゲットとビューポートを退避
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());

    UINT numViewports = 1;
    D3D11_VIEWPORT oldViewport;
    dc->RSGetViewports(&numViewports, &oldViewport);

    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex)
    {
        shadowMap->Activate(dc, cascadeIndex);
        _psoShadow.Apply(dc, rc.renderState);

        DirectX::XMFLOAT3 lightDirection = rc.lightManager->GetDirectionalLight().direction;

        DirectX::XMMATRIX lightVPs[3];
        CalculateCascadeMatrices(
            XMLoadFloat4x4(&rc.camera->GetView()),
            rc.camera->GetFovY(),
            rc.camera->GetAspect(),
            lightDirection,
            shadowMap->params.cascadeSplits,
            lightVPs
        );

        ShadowMap::CbShadow cbData{};
        XMStoreFloat4x4(&cbData.lightViewProjection[0], lightVPs[0]);
        XMStoreFloat4x4(&cbData.lightViewProjection[1], lightVPs[1]);
        XMStoreFloat4x4(&cbData.lightViewProjection[2], lightVPs[2]);
        cbData.cascadeSplits = { shadowMap->params.cascadeSplits[0],
                                      shadowMap->params.cascadeSplits[1],
                                      shadowMap->params.cascadeSplits[2], 0.0f };
        cbData.shadowColor = shadowMap->params.shadowColor;
        cbData.shadowAttenuation = shadowMap->params.shadowAttenuation;
        cbData.shadowBias = shadowMap->params.shadowBias;
        cbData.currentCascadeIndex = cascadeIndex;

        shadowMap->UpdateConstantBuffer(dc, cbData);
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);

        // ShaderRegistry::Instance() / Graphics::Instance() → services 経由に変更
        Shader* shader = _services->shaders->GetShader("ShadowMap"_hash, _services->device);
        if (!shader) {
            shadowMap->Deactivate(dc);
            return;
        }

        shader->Begin(rc);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct ShadowSortItem {
            const Model::Mesh* mesh;
            DirectX::XMFLOAT4X4 worldMatrix;
            uint8_t              cascadeMask;
        };

        std::vector<ShadowSortItem> sortedItems;
        sortedItems.reserve(shadowCommands.size());

        const Model::Mesh* lastBoundMesh = nullptr;

        for (const auto& cmd : shadowCommands) {
            if (!cmd.mesh) continue;

            ShadowSortItem item;
            item.mesh = cmd.mesh;
            item.cascadeMask = cmd.cascadeMask;

            const bool isSkinned = !cmd.mesh->bones.empty();
            if (!isSkinned && cmd.mesh->node) {
                const DirectX::XMMATRIX meshLocal = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform);
                const DirectX::XMMATRIX entityWorld = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                DirectX::XMStoreFloat4x4(&item.worldMatrix, meshLocal * entityWorld);
            }
            else {
                item.worldMatrix = cmd.worldMatrix;
            }
            sortedItems.push_back(item);
        }

        std::sort(sortedItems.begin(), sortedItems.end(),
            [](const ShadowSortItem& a, const ShadowSortItem& b) {
                return a.mesh < b.mesh;
            });

        struct InstanceBatch {
            const Model::Mesh* mesh = nullptr;
            std::vector<InstanceData> data;
        };
        std::vector<InstanceBatch> batches;
        batches.reserve(16);

        for (const auto& info : sortedItems) {
            if (!info.mesh || !info.mesh->data) continue;
            if ((info.cascadeMask & (1 << cascadeIndex)) == 0) continue;

            const bool isSkinned = !info.mesh->bones.empty();
            if (isSkinned) {
                renderer->UpdateSkeletonConstants(dc, *info.mesh);

                if (lastBoundMesh != info.mesh) {
                    UINT stride = sizeof(ModelResource::Vertex);
                    UINT offset = 0;
                    dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    shader->Update(rc, *info.mesh);
                    lastBoundMesh = info.mesh;
                }
                dc->DrawIndexed(static_cast<UINT>(info.mesh->data->indices.size()), 0, 0);
            }
            else {
                if (!batches.empty() && batches.back().mesh == info.mesh) {
                    batches.back().data.push_back({ info.worldMatrix });
                }
                else {
                    InstanceBatch newBatch;
                    newBatch.mesh = info.mesh;
                    newBatch.data.push_back({ info.worldMatrix });
                    batches.push_back(std::move(newBatch));
                }
            }
        }

        for (const auto& batch : batches) {
            shader->Update(rc, *batch.mesh);

            UINT stride = sizeof(ModelResource::Vertex);
            UINT zero = 0;
            dc->IASetVertexBuffers(0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            size_t total = batch.data.size();
            size_t offset = 0;
            while (offset < total) {
                // InstanceBuffer::MAX_INSTANCES → GetMaxInstances() に統一
                const size_t drawCount = (std::min)(total - offset,
                    _instanceBuffer.GetMaxInstances());
                _instanceBuffer.Write(dc, batch.data.data() + offset, drawCount);
                _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                dc->DrawIndexedInstanced(static_cast<UINT>(batch.mesh->data->indices.size()),
                    static_cast<UINT>(drawCount), 0, 0, 0);
                offset += drawCount;
            }
        }

        shader->End(rc);
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }

    // メインキャンバスを復元
    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldViewport);
}