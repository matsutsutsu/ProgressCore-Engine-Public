#include "NormalPrepass.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Renderer/Core/RenderDebugRegistry.h"
#include "Engine/Graphics/Renderer/Core/MeshCulling.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "tracy/Tracy.hpp"
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



// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void NormalPrepass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);

    // 深度も書く。OpaquePass で、隠れたピクセルの重いシェーダーを early-Z で飛ばすため
    // 比較は LESS_EQUAL なので、OpaquePass が同じ深度で描き直しても弾かれない
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
    // 1. 描く準備（コマンドの並べ替え・描画先を法線バッファへ切り替え）
    // 2. バッチごとに、不透明なメッシュの深度と法線を描く
    // 3. 後片付け（描画先を戻し、法線バッファを後のパスへ渡す）

    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue *queue = m_services->renderQueue;
    if (!queue || !m_services->modelConstants) return;

    // ---------------------------------------------------------
    // 1. 描く準備
    // ---------------------------------------------------------

    // 不透明のコマンドを、シェーダー・モデル・マテリアルのキーで並べる
    // OpaquePass より先に走るので、並べるのはこのパス。OpaquePass の呼び出しは何もしない
    queue->SortAndBuildBatches();

    Shader *shader = m_services->shaders->GetShader("NormalPrepass"_hash, m_services->device);
    if (!shader) return;
    if (!ctx.camera) return;

    // 今の描画先とビューポートを控える（3 で戻す）
    // 戻さないと、後のパスまで法線バッファに描いてしまう
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT           numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    // 法線バッファを描画先にし、全バッチで共通のものをバインドする
    BindCommonResources(dc, ctx, *shader);

    m_lodTotals = {};

    // ---------------------------------------------------------
    // 2. バッチごとに描く
    // ---------------------------------------------------------

    const auto &sortKeys = queue->GetSortedKeys();

    size_t cursor = 0;
    while (cursor < sortKeys.size())
    {
        // 同じキーが続く区間を1つのバッチとして切り出し、cursor を次のバッチへ進める
        // 区切り方は OpaquePass と同じにする。残した列を（バッチの先頭, メッシュ）で引くため
        const BatchRange batch = ConsumeBatch(sortKeys, cursor);

        // バッチのモデルの不透明なメッシュを描き、見えるインスタンスの列を OpaquePass 用に残す
        DrawBatch(ctx, batch);
    }

    // ---------------------------------------------------------
    // 3. 後片付け
    // ---------------------------------------------------------

    // LOD の段ごとのインスタンスの数を Tracy のグラフに出す
#if defined(TRACY_ENABLE)
    for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
    {
        TracyPlot(kPlotLodInstances[level], static_cast<int64_t>(m_lodTotals[level]));
    }
#endif

    // シェーダーがバインドした定数バッファとテクスチャを外す
    shader->End(dc, *m_services);

    // 1 で控えた描画先とビューポートに戻す
    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // 後のパスのシェーダーが t6 から読めるようにバインドする
    // 描画先から外した後に行う。同じテクスチャを描画先と読み込みの両方にはバインドできない
    ID3D11ShaderResourceView *normalSRV = m_normalBuffer.GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &normalSRV);

    // デバッグ表示に登録する（描画先からは上で外したので、SRV として読める）
    RenderDebugRegistry::Instance().Register({"NormalPrepass", normalSRV,
                                              m_normalBuffer.GetWidth(), m_normalBuffer.GetHeight(), DebugTexKind::Normal});
}



// ---------------------------------------------------------------------------
// バッチの描画
// ---------------------------------------------------------------------------

void NormalPrepass::BindCommonResources(ID3D11DeviceContext *dc, const RenderFrameContext &ctx, Shader &shader)
{
    // 法線バッファをクリアして描画先にする。深度はシーンの深度バッファにそのまま書く
    m_normalBuffer.Activate(dc, ctx.targetDSV);

    // ブレンドなし・深度を書く・裏面カリングにする
    m_pso.Apply(dc, m_services->renderState);

    // カメラ行列（CbScene）とボーン行列（CbSkeleton）の定数バッファをバインドする
    m_services->modelConstants->BindAllGlobalConstantBuffers(dc);

    // 法線用の頂点・ピクセルシェーダーと入力レイアウトを設定する
    shader.Begin(dc, *m_services, ctx);
}



NormalPrepass::BatchRange NormalPrepass::ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor)
{
    BatchRange     batch{cursor, 0};
    const uint64_t batchKey = sortKeys[cursor].value;
    while (cursor < sortKeys.size() && sortKeys[cursor].value == batchKey)
    {
        ++batch.count;
        ++cursor;
    }
    return batch;
}



void NormalPrepass::DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch)
{
    ID3D11DeviceContext *dc       = ctx.dc;
    RenderQueue         *queue    = m_services->renderQueue;
    const auto          &sortKeys = queue->GetSortedKeys();
    const auto          &commands = queue->GetOpaqueCommands();
    const RenderCommand &first    = commands[sortKeys[batch.start].originalIndex];

    // バッチのコマンドはすべて同じモデルなので、先頭のコマンドのメッシュの並びで回す
    if (!first.model) return;

    const auto &meshes = first.model->GetMeshes();
    for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
    {
        const Model::Mesh &mesh = meshes[meshIdx];
        if (!mesh.data) continue;

        // 不透明のメッシュだけ深度を書く（マテリアルの選び方は OpaquePass と同じ）
        //   半透明（Blend）: 書くと、その奥にあるものが OpaquePass で消える
        //   くり抜き（Mask）: テクスチャを読まず捨てられないので、穴の部分まで深度が残る
        //                    深度は OpaquePass が discard した後の形で書く
        const MaterialData *overrideMat = ResolveOverrideMaterial(first, meshIdx);
        const MaterialData *activeMat =
            overrideMat ? overrideMat : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);
        if (activeMat && activeMat->alphaMode != AlphaMode::Opaque) continue;

        const bool isSkinned = !mesh.bones.empty();

        // OpaquePass と同じ MeshCull を使い、両方のパスでカリングの結果をそろえる
        const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

        // 見えるインスタンスを m_instanceScratch に集め、LOD の段の順に並べる
        const MeshLod::LevelCounts levelCounts = BuildVisibleInstances(ctx, mesh, batch, localAABB, isSkinned);

        // Tracy のグラフ用に、LOD のあるメッシュの段ごとの数を足す
        if (!mesh.data->lods.empty())
        {
            for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
            {
                m_lodTotals[level] += levelCounts[level];
            }
        }

        // 見えるインスタンスの列と段ごとの数を残す。OpaquePass はこれを使い、カリングと段の選択を省く
        // 全部カリングされて空でも残す。OpaquePass が「求め済みで 0 個」と分かり、求め直さないため
        queue->StoreVisibleInstances(
            RenderQueue::MakeVisibleKey(batch.start, meshIdx),
            m_instanceScratch.data(), m_instanceScratch.size(), levelCounts);

        if (m_instanceScratch.empty()) continue;

        // 頂点・インデックスバッファとトポロジを設定する
        BindMeshIA(dc, mesh);

        // スキンメッシュは1体ずつ、それ以外は LOD の段ごとにインスタンシングで描く
        if (isSkinned)
        {
            DrawSkinnedInstances(dc, mesh, meshIdx, batch);
        }
        else
        {
            DrawInstancedBatched(dc, mesh, levelCounts);
        }

        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }
}



const MaterialData *NormalPrepass::ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const
{
    if (meshIdx >= cmd.overrideCount) return nullptr;
    const MaterialHandle handle = cmd.overrideMaterials[meshIdx];
    if (!handle.IsValid()) return nullptr;
    return m_services->resources->GetMaterial(handle);
}



// ---------------------------------------------------------------------------
// 見えるインスタンスの収集
// ---------------------------------------------------------------------------

MeshLod::LevelCounts NormalPrepass::BuildVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh,
                                                          const BatchRange &batch, const DirectX::BoundingBox &localAABB,
                                                          bool isSkinned)
{
    const auto                     &sortKeys = m_services->renderQueue->GetSortedKeys();
    const auto                     &commands = m_services->renderQueue->GetOpaqueCommands();
    const DirectX::BoundingFrustum &frustum  = ctx.camera->GetFrustum();

    m_instanceScratch.clear();
    m_instanceScratch.reserve(batch.count);

    for (size_t k = 0; k < batch.count; ++k)
    {
        const RenderCommand &cmd = commands[sortKeys[batch.start + k].originalIndex];

        InstanceData data;
        data.customParams = cmd.customParams;

        if (isSkinned)
        {
            // スキンメッシュはカリングせず全部積む
            // ワールド行列はボーン行列に入っているので単位行列
            DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
            m_instanceScratch.push_back(data);
            continue;
        }

        data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
        if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue;
        m_instanceScratch.push_back(data);
    }

    // インスタンスごとに画面に映る大きさで段を選び、LOD0 から段の順に並べる
    // スキンメッシュは LOD を持たないので、すべて LOD0 のまま
    if (isSkinned) return MeshLod::AllInLevel0(m_instanceScratch.size());

    return MeshLod::SortInstancesByLevel(m_instanceScratch, m_lodSortTemp, mesh, localAABB,
                                         ctx.camera->GetEye(), ctx.camera->GetFovY());
}



// ---------------------------------------------------------------------------
// インスタンス描画
// ---------------------------------------------------------------------------

void NormalPrepass::BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    const UINT stride = sizeof(ModelResource::Vertex);
    const UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}



void NormalPrepass::DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh,
                                         const MeshLod::LevelCounts &levelCounts)
{
    // m_instanceScratch は段の順に並んでいるので、段ごとに区切って、その段の範囲のインデックスで描く
    // LOD がないメッシュは、段0に全部入っている
    size_t levelStart = 0; // この段の先頭が m_instanceScratch の何番目か
    for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
    {
        const size_t levelCount = levelCounts[level];
        if (levelCount == 0) continue;

        const MeshLod::DrawRange range = MeshLod::GetDrawRange(*mesh.data, level);
        dc->IASetIndexBuffer(range.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

        // インスタンスバッファに入る数ずつ書き込んで描く。入りきらない分は次の描画に回す
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



void NormalPrepass::DrawSkinnedInstances(ID3D11DeviceContext *dc, const Model::Mesh &mesh, size_t meshIdx,
                                         const BatchRange &batch)
{
    // スキンメッシュはカリングしていないので、
    // m_instanceScratch の k 番目はバッチの k 番目のコマンド
    const auto &sortKeys = m_services->renderQueue->GetSortedKeys();
    const auto &commands = m_services->renderQueue->GetOpaqueCommands();

    for (size_t k = 0; k < m_instanceScratch.size(); ++k)
    {
        const RenderCommand &cmd = commands[sortKeys[batch.start + k].originalIndex];
        m_instanceBuffer.Write(dc, &m_instanceScratch[k], 1);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // この1体のボーン行列を CbSkeleton に書く
        m_services->modelConstants->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);

        dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
    }
}
