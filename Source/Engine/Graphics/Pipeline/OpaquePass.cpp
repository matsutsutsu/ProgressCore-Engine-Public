#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <algorithm>

void OpaquePass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services); // _services を保持（基底の規約）
    _psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(services.device, 8192);
}

void OpaquePass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Opaque");
#endif

    // 旧 Shader / Renderer API 用の互換ビューを構築する（移行期のブリッジ）。
    // Shader 側が services+ctx を取る形に更新できたら MakeLegacyContext ごと削除する。
    RenderContext rc = MakeLegacyContext(*_services, ctx);

    ID3D11DeviceContext* dc = ctx.dc;
    if (!dc) return;

    RenderQueue* queue = rc.renderQueue;
    ModelRenderer* renderer = rc.modelRenderer;
    if (!queue || !renderer) return;

    if (rc.shadowMap) {
        rc.shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    ID3D11SamplerState* samplerStates[] = {
        rc.renderState->GetSamplerState(SamplerState::LinearWrap)
    };
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplerStates);

    _psoOpaque.Apply(dc, rc.renderState);

    const DirectX::XMVECTOR cameraPosition = DirectX::XMLoadFloat3(&rc.camera->GetEye());

    queue->SortAndBuildBatches();

    const auto& sortKeys = queue->GetSortedKeys();
    const auto& opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        // シングルトンではなく注入された services 経由で取得（DI 設計の徹底）。
        Shader* shader = _services->shaders->GetShader(current.shaderHash, _services->device);
        if (!shader) {
            ++i;
            continue;
        }

        shader->Begin(rc);

        // --- バッチ範囲を確定（同一ソートキーが連続する区間） ---
        const size_t   batchStart = i;
        size_t         batchCount = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey) {
            ++batchCount;
            ++i;
        }

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];

            // -------------------------------------------------------------
            //  マテリアルオーバーライド
            //  NOTE: 共有 Model の material を const_cast で一時的に書き換えている。
            //        単一スレッド描画前提でのみ安全。将来はシェーダー側へ override を
            //        引数渡しする設計に変更すること（②問題点 参照）。
            // -------------------------------------------------------------
            Model::Mesh& mutableMesh = const_cast<Model::Mesh&>(mesh);
            Model::Material* originalMatPtr = mutableMesh.material;
            MaterialData* overrideData = nullptr;

            if (meshIdx < current.overrideCount) {
                const MaterialHandle handle = current.overrideMaterials[meshIdx];
                if (handle.IsValid()) {
                    overrideData = _services->resources->GetMaterial(handle);
                }
            }

            Model::Material tempMatWrapper;
            const bool applyOverride = (overrideData != nullptr);
            if (applyOverride) {
                tempMatWrapper.data = std::shared_ptr<MaterialData>(overrideData, [](MaterialData*) {});
                tempMatWrapper.name = "InstanceOverride";
                mutableMesh.material = &tempMatWrapper;
            }

            // --- 透明判定 ---
            auto& matData = *mutableMesh.material->data;
            float alpha = 1.0f;
            DirectX::XMFLOAT4 matColor;
            if (matData.GetColor("materialColor"_hash, matColor)) {
                alpha = matColor.w;
            }
            const bool isTransparent = (matData.alphaMode == AlphaMode::Blend ||
                (alpha > 0.01f && alpha < 0.99f));
            const bool isSkinned = !mesh.bones.empty();

            // -------------------------------------------------------------
            //  インスタンスデータ構築（メンバスクラッチを再利用）
            //  - スキンド : world は単位行列（シェーダーの Skinning() が完結）
            //  - 静的     : mesh.node のモデル内オフセット × エンティティのワールド行列
            //  これを以降の「半透明転送」「スキンド描画」「静的描画」全てで使い回す。
            // -------------------------------------------------------------
            _instanceScratch.clear();
            _instanceScratch.reserve(batchCount);

            for (size_t k = 0; k < batchCount; ++k) {
                const uint32_t idx = sortKeys[batchStart + k].originalIndex;
                const auto& cmd = opaqueCommands[idx];

                InstanceData data;
                data.customParams = cmd.customParams;

                if (isSkinned) {
                    DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
                }
                else if (mesh.node) {
                    const DirectX::XMMATRIX meshLocal = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                    const DirectX::XMMATRIX entityWorld = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                    DirectX::XMStoreFloat4x4(&data.world, meshLocal * entityWorld);
                }
                else {
                    data.world = cmd.worldMatrix; // node が null のフォールバック
                }
                _instanceScratch.push_back(data);
            }

            if (isTransparent) {
                // --- 半透明：TransparentPass へ転送 ---
                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& info = opaqueCommands[sortKeys[batchStart + k].originalIndex];

                    TransparencyCommand tInfo;
                    tInfo.shaderHash = current.shaderHash;
                    tInfo.mesh = &mesh;
                    tInfo.customParams = info.customParams;
                    tInfo.overrideMaterial = (meshIdx < info.overrideCount)
                        ? info.overrideMaterials[meshIdx]
                        : MaterialHandle{};

                    // ノード合成済みの行列を格納する
                    if (!isSkinned && mesh.node) {
                        const DirectX::XMMATRIX meshLocal = DirectX::XMLoadFloat4x4(&mesh.node->globalTransform);
                        const DirectX::XMMATRIX entityWorld = DirectX::XMLoadFloat4x4(&info.worldMatrix);
                        DirectX::XMStoreFloat4x4(&tInfo.worldMatrix, meshLocal * entityWorld);
                    }
                    else {
                        tInfo.worldMatrix = info.worldMatrix;
                    }

                    // ソート用の距離はノード合成後の座標から算出
                    const DirectX::XMVECTOR worldPos = DirectX::XMLoadFloat4x4(&tInfo.worldMatrix).r[3];
                    tInfo.distance = DirectX::XMVectorGetX(
                        DirectX::XMVector3Length(DirectX::XMVectorSubtract(worldPos, cameraPosition)));

                    queue->SubmitDeferredTransparent(tInfo);
                }
            }
            else if (isSkinned) {
                // --- スキンド：インスタンシング不可、1 体ずつ描画 ---
                shader->Update(rc, mesh);

                const UINT stride = sizeof(ModelResource::Vertex);
                const UINT offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                for (size_t k = 0; k < batchCount; ++k) {
                    const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];

                    // 上で構築済みのスクラッチ（単位行列 + customParams）をそのまま使う
                    _instanceBuffer.Write(dc, &_instanceScratch[k], 1);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

                    // 描画対象エンティティ（k 体目）の同 meshIdx からボーン行列を送る
                    const Model::Mesh& instanceMesh = cmd.model->GetMeshes()[meshIdx];
                    renderer->UpdateSkeletonConstants(dc, instanceMesh);

                    // 1 体だけインスタンス描画（SV_InstanceID を機能させるため）
                    dc->DrawIndexedInstanced(
                        static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else {
                // --- 静的：インスタンシング描画 ---
                shader->Update(rc, mesh);

                const UINT stride = sizeof(ModelResource::Vertex);
                const UINT offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // ★ 修正：上で構築済みの _instanceScratch を使う。
                //   旧コードはここで関数 static を再定義して world だけ詰め直しており、
                //   (1) node 合成行列が無視される (2) customParams が欠落する
                //   という二重バグがあった。スクラッチ統一でいずれも解消。
                size_t currentOffset = 0;
                while (currentOffset < batchCount) {
                    size_t drawCount = (std::min)(batchCount - currentOffset,
                        _instanceBuffer.GetMaxInstances());

                    _instanceBuffer.Write(dc, _instanceScratch.data() + currentOffset, drawCount);
                    _instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

                    dc->DrawIndexedInstanced(
                        static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(drawCount), 0, 0, 0);

                    currentOffset += drawCount;
                }
            }

            if (applyOverride) mutableMesh.material = originalMatPtr;

            _instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }

        shader->End(rc);
    }
}