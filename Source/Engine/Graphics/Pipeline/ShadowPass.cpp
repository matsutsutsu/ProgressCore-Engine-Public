#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
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

void ShadowPass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);
    _psoShadow = PipelineState(PipelineStateDesc::DefaultShadow());
    _instanceBuffer.Create(services.device, 8192);
}

void ShadowPass::Execute(const RenderFrameContext& ctx)
{

    // MakeLegacyContext 不要: _services / ctx を直接参照
    RenderQueue* queue = _services->renderQueue;
    ModelRenderer* renderer = _services->modelRenderer;
    if (!queue || !renderer) return;

    const auto& shadowCommands = queue->GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext* dc = ctx.dc;
    auto& shadowMap = _services->shadowMap;

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex) {
        shadowMap->Activate(dc, cascadeIndex);
        _psoShadow.Apply(dc, _services->renderState);

        // ctx.camera / _services->lightManager を直接参照
        const DirectX::XMFLOAT3 lightDir =
            _services->lightManager->GetDirectionalLight().direction;

        DirectX::XMMATRIX lightVPs[3];
        CalculateCascadeMatrices(
            XMLoadFloat4x4(&ctx.camera->GetView()),
            ctx.camera->GetFovY(),
            ctx.camera->GetAspect(),
            lightDir,
            shadowMap->params.cascadeSplits,
            lightVPs);

        GpuData::CbShadow cbData{};
        XMStoreFloat4x4(&cbData.light_view_projection[0], lightVPs[0]);
        XMStoreFloat4x4(&cbData.light_view_projection[1], lightVPs[1]);
        XMStoreFloat4x4(&cbData.light_view_projection[2], lightVPs[2]);
        cbData.cascadeSplits = { shadowMap->params.cascadeSplits[0],
                                     shadowMap->params.cascadeSplits[1],
                                     shadowMap->params.cascadeSplits[2], 0.0f };

        cbData.shadow_color = shadowMap->params.shadowColor;
        cbData.shadow_attenuation = shadowMap->params.shadowAttenuation;
        cbData.shadow_bias = shadowMap->params.shadowBias;
        cbData.currentCascadeIndex = cascadeIndex;
        shadowMap->UpdateConstantBuffer(dc, cbData);
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);

        Shader* shader = _services->shaders->GetShader("ShadowMap"_hash, _services->device);
        if (!shader) { shadowMap->Deactivate(dc); return; }

        shader->Begin(dc, *_services, ctx);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ★ヒープ確保を避けるため、メンバのスクラッチ領域を使い回す（.clear()はcapacityを保持する）。
        _itemsScratch.clear();
        _itemsScratch.reserve(shadowCommands.size());

        const Model::Mesh* lastBound = nullptr;

        for (const auto& cmd : shadowCommands) {
            if (!cmd.mesh) continue;
            SortItem item;
            item.mesh = cmd.mesh;
            item.cascadeMask = cmd.cascadeMask;
            if (!cmd.mesh->bones.empty() || !cmd.mesh->node) {
                item.worldMatrix = cmd.worldMatrix;
            }
            else {
                const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform);
                const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                DirectX::XMStoreFloat4x4(&item.worldMatrix, ml * ew);
            }
            _itemsScratch.push_back(item);
        }
        std::sort(_itemsScratch.begin(), _itemsScratch.end(),
            [](const SortItem& a, const SortItem& b) { return a.mesh < b.mesh; });

        // batchesも同様にメンバのプールを使い回す。Batch::data の容量も維持されるため
        // 数フレームで確保が収束する（RenderTargetVisualizer の RT プールと同じ考え方）。
        _batchCount = 0;
        auto acquireBatch = [&]() -> Batch& {
            if (_batchCount >= _batchesScratch.size())
                _batchesScratch.emplace_back();
            Batch& b = _batchesScratch[_batchCount++];
            b.mesh = nullptr;
            b.data.clear();
            return b;
        };

        for (const auto& info : _itemsScratch) {
            if (!info.mesh || !info.mesh->data) continue;
            if ((info.cascadeMask & (1 << cascadeIndex)) == 0) continue;

            const bool isSkinned = !info.mesh->bones.empty();
            if (isSkinned) {
                renderer->UpdateSkeletonConstants(dc, *info.mesh);
                if (lastBound != info.mesh) {
                    UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                    dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

                    lastBound = info.mesh;
                }
                dc->DrawIndexed(static_cast<UINT>(info.mesh->data->indices.size()), 0, 0);
            }
            else {
                if (_batchCount > 0 && _batchesScratch[_batchCount - 1].mesh == info.mesh) {
                    _batchesScratch[_batchCount - 1].data.push_back({ info.worldMatrix });
                }
                else {
                    Batch& b = acquireBatch();
                    b.mesh = info.mesh;
                    b.data.push_back({ info.worldMatrix });
                }
            }
        }

        for (size_t bi = 0; bi < _batchCount; ++bi) {
            const Batch& batch = _batchesScratch[bi];

            UINT stride = sizeof(ModelResource::Vertex), zero = 0;
            dc->IASetVertexBuffers(0, 1, batch.mesh->data->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(batch.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            size_t total = batch.data.size(), offset = 0;
            while (offset < total) {
                const size_t n = (std::min)(total - offset, _instanceBuffer.GetMaxInstances());
                _instanceBuffer.Write(dc, batch.data.data() + offset, n);
                _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                dc->DrawIndexedInstanced(
                    static_cast<UINT>(batch.mesh->data->indices.size()),
                    static_cast<UINT>(n), 0, 0, 0);
                offset += n;
            }
        }

        shader->End(dc, *_services);
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // デバッグ表示へ登録（Texture2DArray のため v1 では一覧に注記のみ。実表示は v2 で対応）
    RenderDebugRegistry::Instance().Register({
        "ShadowMap (Cascades)", shadowMap->GetShaderResourceView(),
        shadowMap->GetWidth(), shadowMap->GetHeight(), DebugTexKind::Array });
}