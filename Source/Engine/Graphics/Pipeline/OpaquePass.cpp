#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/MeshCulling.h" // メッシュ単位カリング（NormalPrepass と共有）
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

// ============================================================================
// OpaquePass
//   役割：
//     ModelRenderSystem が積んだ不透明 RenderCommand を消費して画面へ描画する。
//     PSO が blend 有効（=半透明）と判定されたメッシュはここでは描かず、距離
//     ソートが必要な TransparentPass 用の遅延キューへ転送する。
//
//   1 フレームの流れ（Execute が上から順に呼ぶ）：
//     1. BindCommonResources  共通リソース（シャドウマップ・サンプラー・不透明PSO）
//     2. SortAndBuildBatches  shader→model→material 順のソートキーを作る
//     3. ConsumeBatch         同一バッチキーの連続コマンドを 1 バッチとして切り出す
//     4. DrawBatch            バッチが指すモデルのメッシュを 1 枚ずつ処理する
//          - ForwardMeshToTransparentQueue : 半透明なら後段へ転送して終わり
//          - BuildVisibleInstances         : 非スキンドはここでフラスタムカリング
//          - DrawInstancedBatched / DrawSkinnedInstances : 実際のドローコール
//
//   カリングの要点：
//     - 視錐台はワールド空間（Camera::UpdateFrustum が invView 変換済み）。
//       メッシュのワールド AABB を直接 frustum.Contains で判定できる。
//     - メッシュのワールド行列は node->globalTransform * entityWorld。AABB も同じ
//       変換で作り、描画とカリングの座標系を一致させる。
//     - スキンドはアニメで実形状がレストポーズ AABB を超えうるため除外（安全側）。
// ============================================================================

void OpaquePass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoOpaque = PipelineState(PipelineStateDesc::DefaultOpaque());
    m_instanceBuffer.Create(services.device, 8192);
}

// ----------------------------------------------------------------------------
//  Execute : このフレームの手順書。個々の工程はヘルパへ委譲する。
// ----------------------------------------------------------------------------
void OpaquePass::Execute(const RenderFrameContext &ctx)
{
    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // カリング統計をフレーム毎にリセット（GetDrawnMeshCount / GetCulledMeshCount で参照）
    m_stat_drawnMeshes  = 0;
    m_stat_culledMeshes = 0;

    BindCommonResources(dc);

    // 現在適用中の PSO。シェーダー固有 PSO と差があるときだけ切り替える。
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
            // 不透明パスはマテリアルシェーダー必須。描けないコマンドは 1 件だけ読み飛ばす。
            ++cursor;
            continue;
        }

        // 透明判定はバッチ単位（シェーダー単位）で決まるので、GPU 状態を触る前に済ませる。
        //   半透明バッチは TransparentPass へ転送するだけで 1 ドローも出さない。
        //   Begin / PSO 適用 / End はそのぶん丸ごと無駄になっていた。
        const PipelineState *batchPso           = shader->GetPSO();
        const bool           batchIsTransparent = batchPso && batchPso->IsBlendEnabled();

        const BatchRange batch = ConsumeBatch(sortKeys, cursor);

        if (batchIsTransparent)
        {
            // 転送のみ。GPU 状態に触れないので lastPso も据え置きでよい。
            DrawBatch(ctx, batch, *shader, *matShader);
            continue;
        }

        matShader->Begin(dc, *m_services, ctx);
        ApplyShaderPso(dc, *shader, lastPso);

        DrawBatch(ctx, batch, *shader, *matShader);

        matShader->End(dc, *m_services);
    }
}

// ----------------------------------------------------------------------------
//  1. 全バッチ共通のバインド
// ----------------------------------------------------------------------------
void OpaquePass::BindCommonResources(ID3D11DeviceContext *dc)
{
    if (m_services->shadowMap)
    {
        m_services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    // 材質テクスチャは異方性フィルタで引く。DDS化でミップチェーンが入ったことにより、
    // トライリニアだと斜めに寝た面（床・壁）で必要より数段粗いミップが選ばれ、
    // 遠景が淡く潰れる。ここは透明パスもそのまま引き継いで使う。
    ID3D11SamplerState *samplers[] = {m_services->renderState->GetSamplerState(SamplerState::AnisotropicWrap)};
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);

    m_psoOpaque.Apply(dc, m_services->renderState);
}

// ----------------------------------------------------------------------------
//  3. 同一バッチキーの連続区間を切り出す
//     同じモデルを大量配置した場合、ここで束ねてインスタンシングが効く。
//     Sponza のような単一モデルは count==1 になり、インスタンシングの恩恵は無い。
// ----------------------------------------------------------------------------
OpaquePass::BatchRange OpaquePass::ConsumeBatch(const std::vector<RenderKey> &sortKeys, size_t &cursor)
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

// シェーダー固有 PSO があれば上書き。無ければパス既定へ戻す。
void OpaquePass::ApplyShaderPso(ID3D11DeviceContext *dc, const Shader &shader, const PipelineState *&lastPso)
{
    const PipelineState *want = shader.GetPSO() ? shader.GetPSO() : &m_psoOpaque;
    if (want != lastPso)
    {
        want->Apply(dc, m_services->renderState);
        lastPso = want;
    }
}

// ----------------------------------------------------------------------------
//  4. バッチが指すモデルの各メッシュを処理する
// ----------------------------------------------------------------------------
void OpaquePass::DrawBatch(const RenderFrameContext &ctx, const BatchRange &batch, const Shader &shader,
                           IMaterialShader &matShader)
{
    ID3D11DeviceContext *dc       = ctx.dc;
    const auto          &sortKeys = m_services->renderQueue->GetSortedKeys();
    const auto          &commands = m_services->renderQueue->GetOpaqueCommands();
    const RenderCommand &first    = commands[sortKeys[batch.start].originalIndex];

    // NormalPrepass 側は同じ位置で null チェックしている。片方だけ無防備にしない。
    if (!first.model) return;

    // 透明判定：シェーダー PSO の blend 設定で行う（cbuffer 変数名に依存しない）。
    // シェーダーはバッチ単位で決まるため、メッシュごとに判定し直す必要はない。
    const PipelineState *matPso             = shader.GetPSO();
    const bool           batchIsTransparent = matPso && matPso->IsBlendEnabled();

    const auto &meshes = first.model->GetMeshes();
    for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
    {
        const Model::Mesh &mesh = meshes[meshIdx];
        if (!mesh.data) continue; // ジオメトリ実体なし：スキップ

        // 有効マテリアルの決定（override 優先。mesh.material は書き換えない）
        MaterialData       *overrideMat = ResolveOverrideMaterial(first, meshIdx);
        const MaterialData *activeMat =
            overrideMat ? overrideMat : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);
        if (!activeMat) continue; // マテリアルなし：スキップ

        const bool isSkinned = !mesh.bones.empty();

        // このメッシュのローカル AABB（全インスタンスで使い回す）
        const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

        if (batchIsTransparent)
        {
            ForwardMeshToTransparentQueue(ctx, mesh, meshIdx, batch, first.shaderHash, localAABB, isSkinned);
            continue; // 半透明はここで完了（不透明描画へは進まない）
        }

        ResolveVisibleInstances(ctx, mesh, batch, meshIdx, localAABB, isSkinned);

        // 可視インスタンスが 0 個なら、このメッシュを丸ごとスキップする。
        // ここが最重要：ドローコール・マテリアル更新・IA 設定・Map/Unmap を全て回避。
        if (m_visibleCount == 0)
        {
            ++m_stat_culledMeshes;
            continue;
        }
        ++m_stat_drawnMeshes;

        // override を matShader.Update に明示的に渡す（mesh.material は書き換えない）
        matShader.Update(dc, mesh, overrideMat);
        BindMeshIA(dc, mesh);

        if (isSkinned)
            DrawSkinnedInstances(ctx, mesh, meshIdx, batch);
        else
            DrawInstancedBatched(dc, mesh);

        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
    }
}

MaterialData *OpaquePass::ResolveOverrideMaterial(const RenderCommand &cmd, size_t meshIdx) const
{
    if (meshIdx >= cmd.overrideCount) return nullptr;
    const MaterialHandle handle = cmd.overrideMaterials[meshIdx];
    if (!handle.IsValid()) return nullptr;
    return m_services->resources->GetMaterial(handle);
}

// ----------------------------------------------------------------------------
//  半透明：距離ソート用に後段へ転送（インスタンス単位でカリング）
// ----------------------------------------------------------------------------
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

        // スキンドはワールド行列がそのまま（骨で変形）、非スキンドは node 合成
        const XMFLOAT4X4 worldM = isSkinned ? info.worldMatrix : MeshCull::ComputeMeshWorld(mesh, info.worldMatrix);

        // 非スキンドのみカリング（スキンドは姿勢で AABB が変わるため除外）
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

// ----------------------------------------------------------------------------
//  不透明：可視インスタンス列の確定
//
//  NormalPrepass（Zプリパス）は本パスと同じソート済みキー列を同じ区切りで走査し、
//  同じ MeshCull でカリングしている。つまり各(バッチ, メッシュ)の可視インスタンス列は
//  完全に同一なので、向こうの結果をそのまま使えば行列合成とフラスタム判定を丸ごと省ける。
//  ドローンのように「同一モデルを大量配置」する構成ほど効く。
//
//  見つからない場合は自分で組み立てる。ミスは異常ではなく、
//    ・blend 有効のバッチは NormalPrepass が飛ばす
//    ・エディタから NormalPrepass 自体を無効化できる
//  という正常な経路で起こる。
// ----------------------------------------------------------------------------
void OpaquePass::ResolveVisibleInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh,
                                         const BatchRange &batch, size_t meshIdx,
                                         const DirectX::BoundingBox &localAABB, bool isSkinned)
{
    const uint64_t key = RenderQueue::MakeVisibleKey(batch.start, meshIdx);
    if (m_services->renderQueue->FindVisibleInstances(key, m_visibleData, m_visibleCount)) return;

    BuildVisibleInstances(ctx, mesh, batch, localAABB, isSkinned);
    m_visibleData  = m_instanceScratch.data();
    m_visibleCount = m_instanceScratch.size();
}

// ----------------------------------------------------------------------------
//  不透明：インスタンスデータ構築（非スキンドはここでカリング）
// ----------------------------------------------------------------------------
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
            // スキンドはカリングせず全数積む（ワールドは単位行列。変形は骨行列側）
            DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
            m_instanceScratch.push_back(data);
            continue;
        }

        data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
        if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue; // 視界外は積まない
        m_instanceScratch.push_back(data);
    }
}

void OpaquePass::BindMeshIA(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    const UINT stride = sizeof(ModelResource::Vertex);
    const UINT offset = 0;
    dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
    dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

// batch.count ではなく「カリング後の可視インスタンス数」で描画する。
// インスタンスバッファの容量を超える場合は複数回に分けて発行する。
void OpaquePass::DrawInstancedBatched(ID3D11DeviceContext *dc, const Model::Mesh &mesh)
{
    const size_t visibleCount = m_visibleCount;
    size_t       curOffset    = 0;
    while (curOffset < visibleCount)
    {
        const size_t drawCount = (std::min)(visibleCount - curOffset, m_instanceBuffer.GetMaxInstances());
        m_instanceBuffer.Write(dc, m_visibleData + curOffset, drawCount);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
        dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), static_cast<UINT>(drawCount), 0, 0, 0);
        curOffset += drawCount;
    }
}

// ボーン行列は 1 体ごとに異なるためインスタンシングでまとめられない。1 体ずつ描く。
// スキンドはカリングしていないため m_visibleCount == batch.count。
void OpaquePass::DrawSkinnedInstances(const RenderFrameContext &ctx, const Model::Mesh &mesh, size_t meshIdx,
                                      const BatchRange &batch)
{
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
