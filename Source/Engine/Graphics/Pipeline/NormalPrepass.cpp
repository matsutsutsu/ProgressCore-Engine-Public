#include "NormalPrepass.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Renderer/Core/RenderDebugRegistry.h"
#include "Engine/Graphics/Renderer/Core/MeshCulling.h"
#include "Engine/Graphics/Renderer/Core/MeshLod.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "tracy/Tracy.hpp"
#include <DirectXCollision.h>
#include <wrl/client.h>
#include <algorithm>

#if defined(TRACY_ENABLE)
namespace
{
    // LOD の段ごとの、このフレームに描いたインスタンスの数のグラフ
    // Tracy は名前のポインタを持ち続けるので、定数で渡す
    const char *const kPlotLodInstances[MeshLod::kMaxLevels] = {
        "LOD0 Instances", "LOD1 Instances", "LOD2 Instances", "LOD3 Instances"};
}
#endif



void NormalPrepass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);

    // 深度も書く（DefaultOpaque は TestAndWrite）
    // OpaquePass で、隠れたピクセルの PBR を early-Z で飛ばせる
    PipelineStateDesc desc = PipelineStateDesc::DefaultOpaque();
    m_pso                  = PipelineState(desc);

    m_instanceBuffer.Create(services.device, 8192);
    m_normalBuffer.Create(services.device, services.screenWidth, services.screenHeight);

    // SRV は作った時点で Graphics に渡す
    // MakeFrameContext はパイプラインの実行前に ctx を作るので、
    // Execute の中で渡すと ctx.sceneNormalSRV が1フレーム遅れる
    PublishNormalSRV();
}



void NormalPrepass::OnResize(uint32_t width, uint32_t height)
{
    if (m_services && m_services->device)
    {
        m_normalBuffer.Resize(m_services->device, width, height);

        // Resize で SRV が作り直される
        // 渡し直さないと、Graphics に解放済みの SRV が残る
        PublishNormalSRV();
    }
}



void NormalPrepass::PublishNormalSRV()
{
    Graphics::Instance().SetSceneNormalSRV(m_normalBuffer.GetSRV());
}



void NormalPrepass::Execute(const RenderFrameContext &ctx)
{
    // 処理フロー:
    // 1. 描く準備（コマンドの並べ替え・描画先の切り替え）
    // 2. バッチのメッシュごとに、見えるインスタンスを集めて LOD の段を選ぶ
    // 3. 見えるインスタンスの列を OpaquePass に残し、段ごとに描く
    // 4. 後片付け（描画先を戻し、法線バッファを後のパスへ渡す）

    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // ---------------------------------------------------------
    // 1. 描く準備
    // ---------------------------------------------------------

    // OpaquePass より先に走るので、ここで並べる
    // OpaquePass も呼ぶが、並べ済みなら何もしない
    queue->SortAndBuildBatches();

    Shader *shader = m_services->shaders->GetShader("NormalPrepass"_hash, m_services->device);
    if (!shader) return;

    if (!ctx.camera) return;
    const DirectX::BoundingFrustum &frustum = ctx.camera->GetFrustum();

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT           numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    m_normalBuffer.Activate(dc, ctx.targetDSV);
    m_pso.Apply(dc, m_services->renderState);

    modelConstants->BindAllGlobalConstantBuffers(dc);

    shader->Begin(dc, *m_services, ctx);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const auto &sortKeys       = queue->GetSortedKeys();
    const auto &opaqueCommands = queue->GetOpaqueCommands();

    MeshLod::LevelCounts lodTotals{}; // LOD のあるメッシュの、段ごとのインスタンスの数（グラフ用）

    size_t i = 0;
    while (i < sortKeys.size())
    {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto    &current      = opaqueCommands[currentIndex];

        const size_t   batchStart      = i;
        size_t         batchCount      = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey)
        {
            ++batchCount;
            ++i;
        }

        if (!current.model) continue;

        const auto &meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
        {
            const Model::Mesh &mesh = meshes[meshIdx];
            if (!mesh.data) continue;

            // ---------------------------------------------------------
            // 2. 見えるインスタンスと LOD の段
            // ---------------------------------------------------------

            // 不透明のメッシュだけ深度を書く（マテリアルの選び方は OpaquePass と同じ）
            //   半透明（Blend）: 書くと、その奥にあるものが OpaquePass で消える
            //   くり抜き（Mask）: テクスチャを読まず捨てられないので、穴の部分まで深度が残る
            //                    深度は OpaquePass が discard した後の形で書く
            const MaterialHandle overrideHandle =
                (meshIdx < current.overrideCount) ? current.overrideMaterials[meshIdx] : MaterialHandle{};
            const MaterialData *overrideMat =
                overrideHandle.IsValid() ? m_services->resources->GetMaterial(overrideHandle) : nullptr;
            const MaterialData *activeMat =
                overrideMat ? overrideMat : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);
            if (activeMat && activeMat->alphaMode != AlphaMode::Opaque) continue;

            const bool isSkinned = !mesh.bones.empty();

            // OpaquePass と同じ MeshCull を使い、両方のパスでカリングの結果をそろえる
            const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

            m_instanceScratch.clear();
            m_instanceScratch.reserve(batchCount);
            for (size_t k = 0; k < batchCount; ++k)
            {
                const auto  &cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                InstanceData data;
                data.customParams = cmd.customParams;
                if (isSkinned)
                {
                    // スキンメッシュはカリングせず全部積む
                    // ワールド行列はボーン行列に入っているので単位行列
                    DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
                    m_instanceScratch.push_back(data);
                }
                else
                {
                    data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
                    if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue;
                    m_instanceScratch.push_back(data);
                }
            }

            // インスタンスごとに段を選び、段の順（LOD0 → LOD1 → …）に並べる
            // スキンメッシュは LOD を持たないので、全部 LOD0 のまま
            MeshLod::LevelCounts levelCounts{};
            if (isSkinned)
            {
                levelCounts = MeshLod::AllInLevel0(m_instanceScratch.size());
            }
            else
            {
                levelCounts = MeshLod::SortInstancesByLevel(m_instanceScratch, m_lodSortTemp, mesh, localAABB,
                                                            ctx.camera->GetEye(), ctx.camera->GetFovY());
            }

            if (!mesh.data->lods.empty())
            {
                for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
                {
                    lodTotals[level] += levelCounts[level];
                }
            }

            // ---------------------------------------------------------
            // 3. OpaquePass へ残して描く
            // ---------------------------------------------------------

            // 見えるインスタンスの列と段ごとの数を残す
            // OpaquePass はこれを使い、カリングと段の選択を省く
            queue->StoreVisibleInstances(
                RenderQueue::MakeVisibleKey(batchStart, meshIdx),
                m_instanceScratch.data(), m_instanceScratch.size(), levelCounts);

            if (m_instanceScratch.empty()) continue;

            UINT stride = sizeof(ModelResource::Vertex), offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            if (isSkinned)
            {
                // スキンメッシュはカリングしていないので、
                // m_instanceScratch の k 番目はバッチの k 番目のコマンド
                for (size_t k = 0; k < m_instanceScratch.size(); ++k)
                {
                    const auto &cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    m_instanceBuffer.Write(dc, &m_instanceScratch[k], 1);
                    m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    modelConstants->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else
            {
                // 段ごとに、その段の範囲のインデックスで描く
                // LOD がないメッシュは、段0に全部入っている
                size_t levelStart = 0; // この段の先頭が m_instanceScratch の何番目か
                for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
                {
                    const size_t levelCount = levelCounts[level];
                    if (levelCount == 0) continue;

                    const MeshLod::DrawRange range = MeshLod::GetDrawRange(*mesh.data, level);
                    dc->IASetIndexBuffer(range.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

                    size_t curOffset = 0;
                    while (curOffset < levelCount)
                    {
                        const size_t drawCount = (std::min)(levelCount - curOffset, m_instanceBuffer.GetMaxInstances());
                        m_instanceBuffer.Write(dc, m_instanceScratch.data() + levelStart + curOffset, drawCount);
                        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                        dc->DrawIndexedInstanced(range.indexCount, static_cast<UINT>(drawCount), range.startIndex, 0, 0);
                        curOffset += drawCount;
                    }
                    levelStart += levelCount;
                }
            }
            m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }
    }

    // ---------------------------------------------------------
    // 4. 後片付け
    // ---------------------------------------------------------

#if defined(TRACY_ENABLE)
    for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
    {
        TracyPlot(kPlotLodInstances[level], static_cast<int64_t>(lodTotals[level]));
    }
#endif

    shader->End(dc, *m_services);

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // 後のパスのシェーダーが t6 から読めるようにバインドする
    ID3D11ShaderResourceView *normalSRV = m_normalBuffer.GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &normalSRV);

    // デバッグ表示に登録する（描画先からは上で外したので、SRV として読める）
    RenderDebugRegistry::Instance().Register({"NormalPrepass", normalSRV,
                                              m_normalBuffer.GetWidth(), m_normalBuffer.GetHeight(), DebugTexKind::Normal});
}
