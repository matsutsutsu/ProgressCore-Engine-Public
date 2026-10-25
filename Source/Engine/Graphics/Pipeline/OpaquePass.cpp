#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Renderer/Core/MeshCulling.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

void OpaquePass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
    m_instanceBuffer.Create(services.device, 8192);
}

void OpaquePass::Execute(const RenderFrameContext &ctx)
{
    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    m_stat_drawnMeshes  = 0;
    m_stat_culledMeshes = 0;

    BindCommonResources(dc);

    const PipelineState *lastPso = &m_psoOpaque;

    queue->SortAndBuildBatches();
    const auto &sortKeys       = queue->GetSortedKeys();
    const auto &opaqueCommands = queue->GetOpaqueCommands();

    size_t cursor = 0;
    while (cursor < sortKeys.size())
    {
        const auto &first = opaqueCommands[sortKeys[cursor].originalIndex];

        Shader          *shader    = m_services->shaders->GetShader(first.shaderHash, m_services->device);
        IMaterialShader *matShader = shader ? shader->AsMaterial() : nullptr;
        if (!matShader)
        {
            // マテリアルシェーダーでなければ描けないので、このコマンドだけ飛ばす
            ++cursor;
            continue;
        }

        // 半透明かどうかはシェーダーで決まる（バッチの中では同じ）。半透明のバッチは
        // TransparentPass へ回すだけで描かないので、Begin・PSO の適用・End を呼ばない
        const PipelineState *batchPso           = shader->GetPSO();
        const bool           batchIsTransparent = batchPso && batchPso->IsBlendEnabled();

        const BatchRange batch = ConsumeBatch(sortKeys, cursor);

        if (batchIsTransparent)
        {
            DrawBatch(ctx, batch, *shader, *matShader);
            continue;
        }

        matShader->Begin(dc, *m_services, ctx);
        ApplyShaderPso(dc, *shader, lastPso);

        DrawBatch(ctx, batch, *shader, *matShader);

        matShader->End(dc, *m_services);
    }
}

// ---------------------------------------------------------------------------
// バッチの描画
// ---------------------------------------------------------------------------

void OpaquePass::BindCommonResources(ID3D11DeviceContext *dc)
{
    if (m_services->shadowMap)
    {
        m_services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    // マテリアルのテクスチャは異方性フィルタで読む。トライリニアだと、斜めに見る面（床・壁）で
    // 必要より粗いミップが選ばれ、遠くがぼやける。TransparentPass はこの設定をそのまま使う
    ID3D11SamplerState *samplers[] = {m_services->renderState->GetSamplerState(SamplerState::AnisotropicWrap)};
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);

    m_psoOpaque.Apply(dc, m_services->renderState);
}

OpaquePass::BatchRange OpaquePass::ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor)
{
    // 同じモデルを多く置くと count が大きくなり、インスタンシングが効く。
    // 1つしか置かないモデルは count == 1 で、1回ずつ描くのと変わらない

    BatchRange     batch{cursor, 0};
    const uint64_t batchKey = sortKeys[cursor].value;
    while (cursor < sortKeys.size() && sortKeys[cursor].value == batchKey)
    {
        ++batch.count;
        ++cursor;
    }
    return batch;
}

void OpaquePass::ApplyShaderPso(ID3D11DeviceContext *dc, const Shader &shader, const PipelineState *&lastPso)
{
    const PipelineState *want = shader.GetPSO() ? shader.GetPSO() : &m_psoOpaque;
    if (want != lastPso)
    {
        want->Apply(dc, m_services->renderState);
        lastPso = want;
    }
}

void OpaquePass::DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch, const Shader &shader,
                           IMaterialShader &matShader)
{
    ID3D11DeviceContext *dc       = ctx.dc;
    const auto          &sortKeys = m_services->renderQueue->GetSortedKeys();
    const auto          &commands = m_services->renderQueue->GetOpaqueCommands();
    const RenderCommand &first    = commands[sortKeys[batch.start].originalIndex];

    if (!first.model) return;

    // 半透明かどうかは、シェーダーの PSO でブレンドが有効かで決める
    const PipelineState *matPso             = shader.GetPSO();
    const bool           batchIsTransparent = matPso && matPso->IsBlendEnabled();

    const auto &meshes = first.model->GetMeshes();
    for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
    {
        const Model::Mesh &mesh = meshes[meshIdx];
        if (!mesh.data) continue;

        // 上書きのマテリアルがあればそれを使う（mesh.material は書き換えない）
        MaterialData       *overrideMat = ResolveOverrideMaterial(first, meshIdx);
        const MaterialData *activeMat =
            overrideMat ? overrideMat : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);
        if (!activeMat) continue;

        const bool isSkinned = !mesh.bones.empty();

        const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

        if (batchIsTransparent)
        {
            ForwardMeshToTransparentQueue(ctx, mesh, meshIdx, batch, first.shaderHash, localAABB, isSkinned);
            continue;
        }

        ResolveVisibleInstances(ctx, mesh, batch, meshIdx, localAABB, isSkinned);

        // 見えるインスタンスが無ければ、マテリアルの更新・IA の設定・描画をすべて飛ばす
        if (m_visibleCount == 0)
        {
            ++m_stat_culledMeshes;
            continue;
        }
        ++m_stat_drawnMeshes;

        matShader.Update(dc, mesh, overrideMat);
        BindMeshIA(dc, mesh);

        if (isSkinned)
            DrawSkinnedInstances(ctx, mesh, meshIdx, batch);
        else
            DrawInstancedBatched(dc, mesh);

        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }
}

// ---------------------------------------------------------------------------
// マテリアルの差し替えと半透明への回送
// ---------------------------------------------------------------------------

MaterialData *OpaquePass::ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const
{
    if (meshIdx >= cmd.overrideCount) return nullptr;
    const MaterialHandle handle = cmd.overrideMaterials[meshIdx];
    if (!handle.IsValid()) return nullptr;
    return m_services->resources->GetMaterial(handle);
}

void OpaquePass::ForwardMeshToTransparentQueue(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                                               const BatchRange &batch, uint32_t shaderHash,
                                               const DirectX::BoundingBox &localAABB, bool isSkinned)
{
    using namespace DirectX;

    RenderQueue           *queue     = m_services->renderQueue;
    const auto            &sortKeys  = queue->GetSortedKeys();
    const auto            &commands  = queue->GetOpaqueCommands();
    const XMVECTOR         cameraPos = XMLoadFloat3(&ctx.camera->GetEye());
    const BoundingFrustum &frustum   = ctx.camera->GetFrustum();

    for (size_t k = 0; k < batch.count; ++k)
    {
        const RenderCommand &info = commands[sortKeys[batch.start + k].originalIndex];

        // スキンメッシュはエンティティのワールド行列のまま、それ以外はノードの行列も掛ける
        const XMFLOAT4X4 worldM = isSkinned ? info.worldMatrix : MeshCull::ComputeMeshWorld(mesh, info.worldMatrix);

        if (!isSkinned && !MeshCull::IsMeshVisible(localAABB, worldM, frustum)) continue;

        TransparencyCommand tInfo;
        tInfo.shaderHash       = shaderHash;
        tInfo.mesh             = &mesh;
        tInfo.customParams     = info.customParams;
        tInfo.overrideMaterial = (meshIdx < info.overrideCount) ? info.overrideMaterials[meshIdx] : MaterialHandle{};
        tInfo.worldMatrix      = worldM;

        const XMVECTOR wp = XMLoadFloat4x4(&tInfo.worldMatrix).r[3];
        tInfo.distance    = XMVectorGetX(XMVector3Length(XMVectorSubtract(wp, cameraPos)));

        queue->SubmitDeferredTransparent(tInfo);
    }
}

// ---------------------------------------------------------------------------
// 見えるインスタンスの収集
// ---------------------------------------------------------------------------

void OpaquePass::ResolveVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh,
                                         const BatchRange &batch, size_t meshIdx,
                                         const DirectX::BoundingBox &localAABB, bool isSkinned)
{
    // NormalPrepass は同じキーの列を同じ区切りで回し、同じ MeshCull でカリングしているので、
    // (バッチ, メッシュ) ごとの見えるインスタンスの列は一致する。それを使えば行列の計算と
    // カリングを省ける（ドローンのように同じモデルを多く置くほど効く）。
    // NormalPrepass が走らなかったとき（シェーダーが取れない・enabled が false）は列が無いので、自分で作る

    const uint64_t key = RenderQueue::MakeVisibleKey(batch.start, meshIdx);
    if (m_services->renderQueue->FindVisibleInstances(key, m_visibleData, m_visibleCount, m_visibleLevelCounts)) return;

    BuildVisibleInstances(ctx, mesh, batch, localAABB, isSkinned);
    m_visibleData  = m_instanceScratch.data();
    m_visibleCount = m_instanceScratch.size();
}

void OpaquePass::BuildVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, const BatchRange &batch,
                                       const DirectX::BoundingBox &localAABB, bool isSkinned)
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
            // スキンメッシュはカリングせず全部積む。ワールド行列はボーン行列に入っているので単位行列
            DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
            m_instanceScratch.push_back(data);
            continue;
        }

        data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
        if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue;
        m_instanceScratch.push_back(data);
    }

    // NormalPrepass の結果が無いときだけここに来るので、LOD の段もここで選ぶ（NormalPrepass と同じ関数で選ぶ）
    m_visibleLevelCounts = isSkinned ? MeshLod::AllInLevel0(m_instanceScratch.size())
                                     : MeshLod::SortInstancesByLevel(m_instanceScratch, m_lodSortTemp, mesh, localAABB,
                                                                     ctx.camera->GetEye(), ctx.camera->GetFovY());
}

// ---------------------------------------------------------------------------
// インスタンス描画
// ---------------------------------------------------------------------------

void OpaquePass::BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    const UINT stride = sizeof(ModelResource::Vertex);
    const UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void OpaquePass::DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    // m_visibleData は段の順（LOD0 → LOD1 → …）に並んでいるので、段ごとに区切って、
    // その段の範囲のインデックスで描く（LOD が無いメッシュは段0に全部入っている）
    size_t levelStart = 0; // この段のインスタンスが m_visibleData の何番目から始まるか
    for (size_t level = 0; level < MeshLod::kMaxLevels; ++level)
    {
        const size_t levelCount = m_visibleLevelCounts[level];
        if (levelCount == 0) continue;

        const MeshLod::DrawRange range = MeshLod::GetDrawRange(*mesh.data, level);
        dc->IASetIndexBuffer(range.indexBuffer, DXGI_FORMAT_R32_UINT, 0);

        size_t curOffset = 0;
        while (curOffset < levelCount)
        {
            const size_t drawCount = (std::min)(levelCount - curOffset, m_instanceBuffer.GetMaxInstances());
            m_instanceBuffer.Write(dc, m_visibleData + levelStart + curOffset, drawCount);
            m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
            dc->DrawIndexedInstanced(range.indexCount, static_cast<UINT>(drawCount), range.startIndex, 0, 0);
            curOffset += drawCount;
        }
        levelStart += levelCount;
    }
}

void OpaquePass::DrawSkinnedInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                                      const BatchRange &batch)
{
    // スキンメッシュはカリングしていないので m_visibleCount == batch.count で、
    // k 番目の見えるインスタンスはバッチの k 番目のコマンドに対応する

    ID3D11DeviceContext  *dc             = ctx.dc;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    const auto           &sortKeys       = m_services->renderQueue->GetSortedKeys();
    const auto           &commands       = m_services->renderQueue->GetOpaqueCommands();

    for (size_t k = 0; k < m_visibleCount; ++k)
    {
        const RenderCommand &cmd = commands[sortKeys[batch.start + k].originalIndex];
        m_instanceBuffer.Write(dc, m_visibleData + k, 1);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
        modelConstants->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
        dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
    }
}
