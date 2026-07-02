#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void OpaquePass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);
    _psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(services.device, 8192);
}

void OpaquePass::Execute(const RenderFrameContext& ctx)
{

    ID3D11DeviceContext* dc = ctx.dc;
    if (!dc) return;

    RenderQueue* queue = _services->renderQueue;
    ModelRenderer* renderer = _services->modelRenderer;
    if (!queue || !renderer) return;

    if (_services->shadowMap) {
        _services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }
    ID3D11SamplerState* samplers[] = {
        _services->renderState->GetSamplerState(SamplerState::LinearWrap)
    };
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);
    _psoOpaque.Apply(dc, _services->renderState);

    const PipelineState *lastPso = &_psoOpaque; 

    const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&ctx.camera->GetEye());

    queue->SortAndBuildBatches();
    const auto& sortKeys = queue->GetSortedKeys();
    const auto& opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        Shader* shader = _services->shaders->GetShader(current.shaderHash, _services->device);
        IMaterialShader* matShader = shader ? shader->AsMaterial() : nullptr;

        if (!matShader) { ++i; continue; }   // 不透明パスはマテリアルシェーダー必須
        matShader->Begin(dc, *_services, ctx);

        // シェーダー固有 PSO があれば上書き。無ければパス既定へ戻す。
        {
            const PipelineState *want = shader->GetPSO() ? shader->GetPSO() : &_psoOpaque;
            if (want != lastPso)
            {
                want->Apply(dc, _services->renderState);
                lastPso = want;
            }
        }

        const size_t   batchStart = i;
        size_t         batchCount = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey) { ++batchCount; ++i; }

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];

            // =========================================================
            // ★ F: const_cast 廃止
            //    override を引数として取得し、mesh.material は一切書き換えない。
            // =========================================================
            MaterialData* overrideMat = nullptr;
            if (meshIdx < current.overrideCount) {
                MaterialHandle h = current.overrideMaterials[meshIdx];
                if (h.IsValid()) overrideMat = _services->resources->GetMaterial(h);
            }

            // 有効なマテリアル（override 優先）を決定する
            const MaterialData* activeMat = overrideMat
                ? overrideMat
                : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);

            if (!activeMat) continue; // マテリアルなし：スキップ

            // 透明判定（activeMat を直接参照。mesh への書き込みゼロ）
            float alpha = 1.0f;
            DirectX::XMFLOAT4 matColor;
            if (activeMat->GetColor("materialColor"_hash, matColor)) alpha = matColor.w;
            const bool isTransparent = (activeMat->alphaMode == AlphaMode::Blend ||
                (alpha > 0.01f && alpha < 0.99f));
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

            _instanceBuffer.Write(dc, _instanceScratch);
            _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

            if (isTransparent) {
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& info = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    TransparencyCommand tInfo;
                    tInfo.shaderHash = current.shaderHash;
                    tInfo.mesh = &mesh;
                    tInfo.customParams = info.customParams;
                    tInfo.overrideMaterial = (meshIdx < info.overrideCount)
                        ? info.overrideMaterials[meshIdx] : MaterialHandle{};
                    if (!isSkinned && mesh.node) {
                        const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                        const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&info.worldMatrix);
                        DirectX::XMStoreFloat4x4(&tInfo.worldMatrix, ml * ew);
                    }
                    else { tInfo.worldMatrix = info.worldMatrix; }
                    const DirectX::XMVECTOR wp = DirectX::XMLoadFloat4x4(&tInfo.worldMatrix).r[3];
                    tInfo.distance = DirectX::XMVectorGetX(
                        DirectX::XMVector3Length(DirectX::XMVectorSubtract(wp, cameraPos)));
                    queue->SubmitDeferredTransparent(tInfo);
                }
            }
            else if (isSkinned) {
                // override を shader->Update に明示的に渡す（mesh.material を書き換えない）
                matShader->Update(dc, mesh, overrideMat);
                UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    _instanceBuffer.Write(dc, &_instanceScratch[k], 1);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    renderer->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else {
                matShader->Update(dc, mesh, overrideMat);
                UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
            // ★ const_cast による restore 行 完全削除
            _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }
        matShader->End(dc, *_services);
    }
}