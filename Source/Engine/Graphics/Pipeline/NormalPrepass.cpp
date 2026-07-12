/// @file  NormalPrepass.cpp
/// @brief NormalPrepass の実装。ShadowPass と同型の「専用シェーダー二度描き」パターン。
#include "NormalPrepass.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <wrl/client.h>
#include <algorithm>

void NormalPrepass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);

    PipelineStateDesc desc = PipelineStateDesc::DefaultOpaque();
    desc.depth = DepthState::TestOnly;
    _pso = PipelineState(desc);

    _instanceBuffer.Create(services.device, 8192);
    _normalBuffer.Create(services.device, services.screenWidth, services.screenHeight);
}

void NormalPrepass::OnResize(uint32_t width, uint32_t height)
{
    if (_services && _services->device) {
        _normalBuffer.Resize(_services->device, width, height);
    }
}

void NormalPrepass::Execute(const RenderFrameContext& ctx)
{
    ID3D11DeviceContext* dc = ctx.dc;
    if (!dc) return;

    RenderQueue* queue = _services->renderQueue;
    ModelRenderer* renderer = _services->modelRenderer;
    if (!queue || !renderer) return;

    Shader* shader = _services->shaders->GetShader("NormalPrepass"_hash, _services->device);
    if (!shader) return;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    _normalBuffer.Activate(dc, ctx.targetDSV);
    _pso.Apply(dc, _services->renderState);

    renderer->BindAllGlobalConstantBuffers(dc);

    shader->Begin(dc, *_services, ctx);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const auto& sortKeys = queue->GetSortedKeys();
    const auto& opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        const size_t   batchStart = i;
        size_t         batchCount = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey) { ++batchCount; ++i; }

        if (!current.model) continue;

        Shader* matShader = _services->shaders->GetShader(current.shaderHash, _services->device);
        if (matShader) {
            const PipelineState* matPso = matShader->GetPSO();
            if (matPso && matPso->IsBlendEnabled()) continue;
        }

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];
            if (!mesh.data) continue;

            const bool isSkinned = !mesh.bones.empty();

            _instanceScratch.clear();
            _instanceScratch.reserve(batchCount);
            for (size_t k = 0; k < batchCount; ++k) {
                const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                InstanceData data;
                data.customParams = cmd.customParams;
                if (isSkinned) {
                    DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
                }
                else if (mesh.node) {
                    const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                    const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                    DirectX::XMStoreFloat4x4(&data.world, ml * ew);
                }
                else { data.world = cmd.worldMatrix; }
                _instanceScratch.push_back(data);
            }

            UINT stride = sizeof(ModelResource::Vertex), offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            if (isSkinned) {
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    _instanceBuffer.Write(dc, &_instanceScratch[k], 1);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    renderer->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else {
                size_t curOffset = 0;
                while (curOffset < batchCount) {
                    const size_t drawCount = (std::min)(
                        batchCount - curOffset, _instanceBuffer.GetMaxInstances());
                    _instanceBuffer.Write(dc, _instanceScratch.data() + curOffset, drawCount);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(drawCount), 0, 0, 0);
                    curOffset += drawCount;
                }
            }
            _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }
    }

    shader->End(dc, *_services);

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    ID3D11ShaderResourceView* normalSRV = _normalBuffer.GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &normalSRV);
    Graphics::Instance().SetSceneNormalSRV(normalSRV);
}
