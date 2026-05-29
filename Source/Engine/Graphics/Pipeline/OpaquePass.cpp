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
#if TRACY_ENABLE
    ZoneScopedN("Pass: Opaque");
#endif
    // MakeLegacyContext 不要: _services / ctx を直接参照
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

    const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&ctx.camera->GetEye());

    queue->SortAndBuildBatches();

    const auto& sortKeys = queue->GetSortedKeys();
    const auto& opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        Shader* shader = _services->shaders->GetShader(current.shaderHash, _services->device);
        if (!shader) { ++i; continue; }

        // 新シェーダー API: Begin(dc, services, ctx)
        shader->Begin(dc, *_services, ctx);

        const size_t   batchStart = i;
        size_t         batchCount = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey) {
            ++batchCount; ++i;
        }

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];

            Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
            Model::Material* origMat = mutableMesh.material;
            MaterialData* overrideData = nullptr;

            if (meshIdx < current.overrideCount) {
                MaterialHandle h = current.overrideMaterials[meshIdx];
                if (h.IsValid()) overrideData = _services->resources->GetMaterial(h);
            }

            Model::Material tempWrapper;
            const bool applyOverride = (overrideData != nullptr);
            if (applyOverride) {
                tempWrapper.data = std::shared_ptr<MaterialData>(overrideData, [](MaterialData*) {});
                tempWrapper.name = "InstanceOverride";
                mutableMesh.material = &tempWrapper;
            }

            auto& matData = *mutableMesh.material->data;
            float alpha = 1.0f;
            DirectX::XMFLOAT4 matColor;
            if (matData.GetColor("materialColor"_hash, matColor)) alpha = matColor.w;

            const bool isTransparent = (matData.alphaMode == AlphaMode::Blend ||
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
                // 新シェーダー API: Update(dc, mesh)
                shader->Update(dc, mesh);
                UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    _instanceBuffer.Write(dc, &_instanceScratch[k], 1);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    renderer->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(
                        static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else {
                shader->Update(dc, mesh);
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
                    dc->DrawIndexedInstanced(
                        static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(drawCount), 0, 0, 0);
                    curOffset += drawCount;
                }
            }

            if (applyOverride) mutableMesh.material = origMat;
            _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }

        // 新シェーダー API: End(dc, services)
        shader->End(dc, *_services);
    }
}