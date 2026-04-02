#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <algorithm>

void ShadowPass::Initialize(ID3D11Device* device) {
    _psoShadow = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(device, 8192);
}

void ShadowPass::Execute(const RenderContext& rc, RenderQueue& queue, ModelRenderer* renderer) {
    const auto& shadowCommands = queue.GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext* dc = rc.deviceContext;
    auto& shadowMap = rc.shadowMap;

    shadowMap->Activate(dc);
    _psoShadow.Apply(dc, rc.renderState);

    shadowMap->params.shadowBias = renderer->GetShadowBias();
    shadowMap->params.shadowColor = renderer->GetShadowColor();

    auto cbData = shadowMap->CreateCbData(renderer->GetShadowLightVP());
    shadowMap->UpdateConstantBuffer(dc, cbData);
    shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);

    Shader* shader = ShaderRegistry::Instance().GetShader("ShadowMap"_hash, Graphics::Instance().GetDevice());
    if (!shader) {
        shadowMap->Deactivate(dc);
        return;
    }

    shader->Begin(rc);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // ソート用のローカル構造体とリスト構築
    struct ShadowSortItem {
        const Model::Mesh* mesh;
        DirectX::XMFLOAT4X4 worldMatrix;
    };
    std::vector<ShadowSortItem> sortedItems;
    sortedItems.reserve(shadowCommands.size());
    for (const auto& cmd : shadowCommands) {
        sortedItems.push_back({ cmd.mesh, cmd.worldMatrix });
    }

    std::sort(sortedItems.begin(), sortedItems.end(),
        [](const ShadowSortItem& a, const ShadowSortItem& b) {
            return a.mesh < b.mesh;
        });

    struct InstanceBatch {
        const Model::Mesh* mesh = nullptr;
        std::vector<InstanceData> data = {};
    };
    std::vector<InstanceBatch> batches;
    batches.reserve(16);

    for (const auto& info : sortedItems) {
        if (!info.mesh || !info.mesh->data) continue;

        bool isSkinned = !info.mesh->bones.empty();
        if (isSkinned) {
            renderer->UpdateSkeletonConstants(dc, *info.mesh);
            UINT stride = sizeof(ModelResource::Vertex);
            UINT offset = 0;
            dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            shader->Update(rc, *info.mesh);
            dc->DrawIndexed((UINT)info.mesh->data->indices.size(), 0, 0);
        }
        else {
            if (!batches.empty() && batches.back().mesh == info.mesh) {
                batches.back().data.push_back({ info.worldMatrix });
            }
            else {
                InstanceBatch newBatch;
                newBatch.mesh = info.mesh;
                newBatch.data.push_back({ info.worldMatrix });
                batches.push_back(newBatch);
            }
        }
    }

    for (const auto& batch : batches) {
        shader->Update(rc, *batch.mesh);
        size_t totalCount = batch.data.size();
        size_t offset = 0;

        while (offset < totalCount) {
            size_t drawCount = (std::min)(totalCount - offset, InstanceBuffer::MAX_INSTANCES);
            std::vector<InstanceData> chunk(batch.data.begin() + offset, batch.data.begin() + offset + drawCount);

            _instanceBuffer.Write(dc, chunk);
            _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

            UINT stride = sizeof(ModelResource::Vertex);
            UINT zero = 0;
            dc->IASetVertexBuffers(0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            dc->DrawIndexedInstanced((UINT)batch.mesh->data->indices.size(), (UINT)drawCount, 0, 0, 0);
            offset += drawCount;
        }
    }

    shader->End(rc);
    _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    shadowMap->Deactivate(dc);
}