#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Core/Graphics.h"
#include "Engine/Graphics/Core/Camera.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Shader/Pass/ShadowUtils.h"
#include <algorithm>


void ShadowPass::Initialize(ID3D11Device* device) {
    _psoShadow = PipelineState(PipelineStateDesc::DefaultOpaque());
    _instanceBuffer.Create(device, 8192);
}

void ShadowPass::Execute(const RenderContext& rc){
#ifdef TRACY_ENABLE
    ZoneScopedN("Pass: Shadow");
#endif

    RenderQueue* queue = rc.renderQueue;
    ModelRenderer* renderer = rc.modelRenderer;

    if (!queue || !renderer) return; // 安全対策

    const auto& shadowCommands = queue->GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext* dc = rc.deviceContext;
    auto& shadowMap = rc.shadowMap;

    // 現在のレンダーターゲット（メインのキャンバス）とビューポートを退避
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());

    UINT numViewports = 1;
    D3D11_VIEWPORT oldViewport;
    dc->RSGetViewports(&numViewports, &oldViewport);

    // カスケードの数だけ描画を繰り返す
    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex)
    {
        // 書き込む対象の層をアクティブにする
        shadowMap->Activate(dc, cascadeIndex);

        // 描画ステートの適用
        _psoShadow.Apply(dc, rc.renderState);

        DirectX::XMFLOAT3 lightDirection = rc.lightManager->GetDirectionalLight().direction;

        // 3つの行列を計算して取得する
        DirectX::XMMATRIX lightVPs[3];


        // ※ cameraのView行列、FOV、アスペクト比、ライトの方向を取得して渡す
        CalculateCascadeMatrices(
            XMLoadFloat4x4(&rc.camera->GetView()),
            rc.camera->GetFovY(),
            rc.camera->GetAspect(),
            lightDirection, // CbSceneから取得しているライト方向
            shadowMap->params.cascadeSplits,
            lightVPs
        );

        // 定数バッファ用データの作成
        ShadowMap::CbShadow cbData{};
        XMStoreFloat4x4(&cbData.lightViewProjection[0], lightVPs[0]);
        XMStoreFloat4x4(&cbData.lightViewProjection[1], lightVPs[1]);
        XMStoreFloat4x4(&cbData.lightViewProjection[2], lightVPs[2]);
        cbData.cascadeSplits = { shadowMap->params.cascadeSplits[0], shadowMap->params.cascadeSplits[1], shadowMap->params.cascadeSplits[2], 0.0f };
        cbData.shadowColor = shadowMap->params.shadowColor;
        cbData.shadowAttenuation = shadowMap->params.shadowAttenuation;
        cbData.shadowBias = shadowMap->params.shadowBias;
        // 今描画している層の番号をセットして定数バッファを更新
        cbData.currentCascadeIndex = cascadeIndex;

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
            uint8_t cascadeMask;
        };
        std::vector<ShadowSortItem> sortedItems;
        sortedItems.reserve(shadowCommands.size());
        for (const auto& cmd : shadowCommands) {
            sortedItems.push_back({ cmd.mesh, cmd.worldMatrix, cmd.cascadeMask });
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

            // ================================================================
            // ここが真の最適なカリング位置。
            // 現在の層(cascadeIndex)の担当外なら、スキンド判定もバッチ登録も全て無視する
            // ================================================================
            if ((info.cascadeMask & (1 << cascadeIndex)) == 0) continue;

            bool isSkinned = !info.mesh->bones.empty();
            if (isSkinned) {
                // スキンドメッシュの個別描画
                renderer->UpdateSkeletonConstants(dc, *info.mesh);
                UINT stride = sizeof(ModelResource::Vertex);
                UINT offset = 0;
                dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                shader->Update(rc, *info.mesh);
                dc->DrawIndexed((UINT)info.mesh->data->indices.size(), 0, 0);
            }
            else {
                // 静的メッシュのバッチ登録
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

    }

    // =========================================================
    // ループを抜けたら、退避しておいたメインキャンバスを復元する
    // =========================================================
    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldViewport);
}