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
    // LOD の段ごとの、このフレームに描いたインスタンスの数のグラフ（Tracy は名前のポインタを持ち続けるので定数で渡す）
    const char *const kPlotLodInstances[MeshLod::kMaxLevels] = {
        "LOD0 Instances", "LOD1 Instances", "LOD2 Instances", "LOD3 Instances"};
} // namespace
#endif


void NormalPrepass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);

    // 深度も書く（DefaultOpaque は TestAndWrite）。ここで不透明の深度を先に決めておくと、
    // OpaquePass では early-Z で隠れたピクセルの PBR のピクセルシェーダーを飛ばせる
    PipelineStateDesc desc = PipelineStateDesc::DefaultOpaque();
    m_pso                  = PipelineState(desc);

    m_instanceBuffer.Create(services.device, 8192);
    m_normalBuffer.Create(services.device, services.screenWidth, services.screenHeight);

    // SRV は作った時点で Graphics に渡す。Graphics::MakeFrameContext はパイプラインを
    // 実行する前に ctx を作るので、Execute の中で渡すと ctx.sceneNormalSRV が1フレーム遅れる
    // （最初のフレームは null になる）。SRV は作り直すとき以外は変わらない
    PublishNormalSRV();
}

void NormalPrepass::OnResize(uint32_t width, uint32_t height)
{
    if (m_services && m_services->device)
    {
        m_normalBuffer.Resize(m_services->device, width, height);
        // Resize で SRV が作り直されるので、渡し直さないと Graphics に解放済みの SRV が残る
        PublishNormalSRV();
    }
}

void NormalPrepass::PublishNormalSRV()
{
    Graphics::Instance().SetSceneNormalSRV(m_normalBuffer.GetSRV());
}

void NormalPrepass::Execute(const RenderFrameContext &ctx)
{
    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // OpaquePass より先に走るので、ここで並べる。OpaquePass も呼ぶが、
    // RenderQueue がそのフレームで並べ済みなら何もしない
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

    MeshLod::LevelCounts lodTotals{}; // LOD のあるメッシュについて、このフレームの段ごとのインスタンスの数（グラフ用）

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

            // 不透明のメッシュだけ深度を書く。
            //   半透明（Blend）: 書くと、その奥にあるものが OpaquePass で消える
            //   くり抜き（Mask）: このパスはテクスチャを読まず捨てられないので、書くと穴の部分まで深度が残る。
            //                    深度は OpaquePass が discard した後の形で書く
            // マテリアルの選び方（上書きがあればそちら）は OpaquePass と同じ
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
                    // スキンメッシュはカリングせず全部積む。ワールド行列はボーン行列に入っているので単位行列
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

            // LOD のあるメッシュは、インスタンスごとに段を選び、段の順（LOD0 → LOD1 → …）に並べる。
            // スキンメッシュは LOD を持たないので、全部 LOD0 のまま
            const MeshLod::LevelCounts levelCounts =
                isSkinned ? MeshLod::AllInLevel0(m_instanceScratch.size())
                          : MeshLod::SortInstancesByLevel(m_instanceScratch, m_lodSortTemp, mesh, localAABB,
                                                          ctx.camera->GetEye(), ctx.camera->GetFovY());

            if (!mesh.data->lods.empty())
            {
                for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
                {
                    lodTotals[level] += levelCounts[level];
                }
            }

            // 見えるインスタンスの列と段ごとの数を OpaquePass に残す。同じバッチの区切りと同じ MeshCull を
            // 使っているので結果は一致し、OpaquePass は行列の計算・カリング・段の選択を省ける。
            // 空の列（全部カリングされた）も「見えるものが無い」という結果として残す
            queue->StoreVisibleInstances(
                RenderQueue::MakeVisibleKey(batchStart, meshIdx),
                m_instanceScratch.data(), m_instanceScratch.size(), levelCounts);

            if (m_instanceScratch.empty()) continue;

            UINT stride = sizeof(ModelResource::Vertex), offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            if (isSkinned)
            {
                // スキンメッシュはカリングしていないので m_instanceScratch.size() == batchCount
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
                // 段ごとに、その段の範囲のインデックスで描く（LOD が無いメッシュは段0に全部入っている）
                size_t levelStart = 0; // この段のインスタンスが m_instanceScratch の何番目から始まるか
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
