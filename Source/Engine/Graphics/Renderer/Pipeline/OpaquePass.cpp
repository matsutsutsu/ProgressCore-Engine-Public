#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Core/Graphics.h"
#include <algorithm>

void OpaquePass::Initialize(ID3D11Device* device) {
    _psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(device, 8192);
}

void OpaquePass::Execute(const RenderContext& rc, RenderQueue& queue, ModelRenderer* renderer) {
    ID3D11DeviceContext* dc = rc.deviceContext;

    // =========================================================
    // ShadowPassで描かれた影のデータを読み込めるようにセットする
    // =========================================================
    if (rc.shadowMap) {
        // SLOT_SRV_SHADOW(t10), SLOT_SMP_SHADOW(s10), SLOT_CB_SHADOW(b10) にバインド
        rc.shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    ID3D11SamplerState* samplerStates[] = { rc.renderState->GetSamplerState(SamplerState::LinearWrap) };
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplerStates);

    _psoOpaque.Apply(dc, rc.renderState);

    // カメラ位置の取得（半透明ソート用）
    DirectX::XMVECTOR CameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetEye());

    // カウンターの伝票をソートさせる
    queue.SortAndBuildBatches();

    const auto& sortKeys = queue.GetSortedKeys();
    const auto& opaqueCommands = queue.GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        Shader* shader = ShaderRegistry::Instance().GetShader(current.shaderHash, Graphics::Instance().GetDevice());
        if (!shader) {
            i++;
            continue;
        }

        shader->Begin(rc);

        size_t batchStart = i;
        size_t batchCount = 0;
        uint64_t currentBatchKey = sortKeys[batchStart].value;

        while (i < sortKeys.size()) {
            if (sortKeys[i].value != currentBatchKey) break;
            batchCount++;
            i++;
        }

        static std::vector<InstanceData> instanceDataCache;
        instanceDataCache.clear();
        instanceDataCache.reserve(batchCount);

        for (size_t k = 0; k < batchCount; ++k) {
            uint32_t idx = sortKeys[batchStart + k].originalIndex;
            const auto& cmd = opaqueCommands[idx];

            InstanceData data;
            data.world = cmd.worldMatrix;
            data.customParams = cmd.customParams;
            instanceDataCache.push_back(data);
        }

        _instanceBuffer.Write(dc, instanceDataCache);
        _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];

            Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
            Model::Material* originalMatPtr = mutableMesh.material;
            MaterialData* overrideData = nullptr;

            if (meshIdx < current.overrideCount) {
                MaterialHandle handle = current.overrideMaterials[meshIdx];
                if (handle.IsValid()) {
                    overrideData = ResourceManager::Instance().GetMaterial(handle);
                }
            }

            Model::Material tempMatWrapper;
            bool applyOverride = (overrideData != nullptr);

            if (applyOverride) {
                tempMatWrapper.data = std::shared_ptr<MaterialData>(overrideData, [](MaterialData*) {});
                tempMatWrapper.name = "InstanceOverride";
                mutableMesh.material = &tempMatWrapper;
            }

            auto& matData = *mutableMesh.material->data;
            float alpha = 1.0f;
            if (matData.colors.count("materialColor")) alpha = matData.colors.at("materialColor").w;
            bool isTransparent = (matData.alphaMode == AlphaMode::Blend || (alpha > 0.01f && alpha < 0.99f));

            if (isTransparent) {
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& info = opaqueCommands[sortKeys[batchStart + k].originalIndex];

                    TransparencyCommand tInfo;
                    tInfo.shaderHash = current.shaderHash;
                    tInfo.mesh = &mesh;
                    tInfo.worldMatrix = info.worldMatrix;
                    if (meshIdx < info.overrideCount) {
                        tInfo.overrideMaterial = info.overrideMaterials[meshIdx];
                    }
                    else {
                        tInfo.overrideMaterial = MaterialHandle{};
                    }
                    tInfo.customParams = info.customParams;

                    DirectX::XMVECTOR WorldPos = DirectX::XMLoadFloat4x4(&info.worldMatrix).r[3];
                    tInfo.distance = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMVectorSubtract(WorldPos, CameraPosition)));

                    // ★次の TransparentPass へ託す！
                    queue.SubmitDeferredTransparent(tInfo);
                }
            }
            else {
                bool isSkinned = !mesh.bones.empty();
                if (isSkinned) {
                    for (size_t k = 0; k < batchCount; ++k) {
                        renderer->UpdateSkeletonConstants(dc, mesh);
                        shader->Update(rc, mesh);
                        UINT stride = sizeof(ModelResource::Vertex);
                        UINT offset = 0;
                        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);
                    }
                }
                else {
                    shader->Update(rc, mesh);
                    UINT stride = sizeof(ModelResource::Vertex);
                    UINT offset = 0;
                    dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), static_cast<UINT>(batchCount), 0, 0, 0);
                }
            }

            if (applyOverride) {
                mutableMesh.material = originalMatPtr;
            }
        }
        _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        shader->End(rc);
    }
}