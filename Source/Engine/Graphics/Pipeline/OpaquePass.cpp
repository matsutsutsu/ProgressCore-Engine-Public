#include "OpaquePass.h"
#include "Engine/Graphics/Renderer/ModelRenderer.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/MeshCulling.h" // メッシュ単位カリング（NormalPrepass と共有）
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <DirectXCollision.h> // フラスタム / AABB カリング用
#include <algorithm>

// ============================================================================
// OpaquePass
//   役割：
//     ModelRenderSystem が積んだ不透明 RenderCommand を消費して画面へ描画する。
//     PSO が blend 有効（=半透明）と判定されたメッシュはここでは描かず、距離
//     ソートが必要な TransparentPass 用の遅延キューへ転送する。
//
//   1 フレームの流れ：
//     ① 共通リソース（シャドウマップ・サンプラー・不透明PSO）をバインド
//     ② RenderQueue をソートしてバッチキーを作る（shader→model→material 順）
//     ③ 同一バッチキー（同じシェーダー＆モデル＆マテリアル）の連続コマンドを束ねる
//     ④ バッチ内の各メッシュについて：
//          - メッシュ単位フラスタムカリング（非スキンドのみ）で可視インスタンスだけ集約
//          - 可視インスタンスが 0 のメッシュはドローコールごと丸ごとスキップ
//          - 残りをまとめてインスタンシング描画（スキンドは骨定数の都合で 1 体ずつ）
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

void OpaquePass::Execute(const RenderFrameContext &ctx)
{
    ID3D11DeviceContext *dc = ctx.dc;
    if (!dc) return;

    RenderQueue   *queue    = m_services->renderQueue;
    ModelRenderer *renderer = m_services->modelRenderer;
    if (!queue || !renderer) return;

    // カリング統計をフレーム毎にリセット（GetDrawnMeshCount / GetCulledMeshCount で参照）
    m_stat_drawnMeshes  = 0;
    m_stat_culledMeshes = 0;

    // ------------------------------------------------------------------
    // ① 共通リソースのバインド（シャドウマップ・サンプラー・不透明PSO）
    // ------------------------------------------------------------------
    if (m_services->shadowMap)
    {
        m_services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }
    ID3D11SamplerState *samplers[] = {
        m_services->renderState->GetSamplerState(SamplerState::LinearWrap)};
    dc->PSSetSamplers(SLOT_SMP_LINEAR, 1, samplers);
    m_psoOpaque.Apply(dc, m_services->renderState);

    // 現在適用中の PSO。シェーダー固有 PSO と差があるときだけ切り替える。
    const PipelineState *lastPso = &m_psoOpaque;

    const DirectX::XMVECTOR cameraPos = DirectX::XMLoadFloat3(&ctx.camera->GetEye());

    // ワールド空間フラスタム（Camera::UpdateFrustum で invView 変換済み）。
    // カリング判定と AABB / ワールド行列の構築は MeshCull ヘルパに集約し、
    // NormalPrepass と完全に同一基準であることを保証する（実装ズレ防止）。
    const DirectX::BoundingFrustum &frustum = ctx.camera->GetFrustum();

    // ------------------------------------------------------------------
    // ② コマンドをソートしてバッチキーを構築（shader→model→material 順）
    // ------------------------------------------------------------------
    queue->SortAndBuildBatches();
    const auto &sortKeys       = queue->GetSortedKeys();
    const auto &opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size())
    {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto    &current      = opaqueCommands[currentIndex];

        Shader          *shader    = m_services->shaders->GetShader(current.shaderHash, m_services->device);
        IMaterialShader *matShader = shader ? shader->AsMaterial() : nullptr;

        if (!matShader)
        {
            ++i;
            continue;
        } // 不透明パスはマテリアルシェーダー必須
        matShader->Begin(dc, *m_services, ctx);

        // シェーダー固有 PSO があれば上書き。無ければパス既定へ戻す。
        {
            const PipelineState *want = shader->GetPSO() ? shader->GetPSO() : &m_psoOpaque;
            if (want != lastPso)
            {
                want->Apply(dc, m_services->renderState);
                lastPso = want;
            }
        }

        // ------------------------------------------------------------------
        // ③ 同一バッチキーを 1 バッチに束ねる。
        //    batchCount = このバッチに属するインスタンス（同一モデル）の個数。
        //    ※同じモデルを大量配置した場合はここで束ねてインスタンシングが効く。
        //    ※Sponza のような単一モデルは batchCount==1（インスタンシングの恩恵は無い）。
        // ------------------------------------------------------------------
        const size_t   batchStart      = i;
        size_t         batchCount      = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey)
        {
            ++batchCount;
            ++i;
        }

        // ------------------------------------------------------------------
        // ④ バッチが指すモデルの各メッシュを描画する
        // ------------------------------------------------------------------
        const auto &meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx)
        {
            const Model::Mesh &mesh = meshes[meshIdx];
            if (!mesh.data) continue; // ジオメトリ実体なし：スキップ

            // --- 有効マテリアルの決定（override 優先。mesh.material は書き換えない）---
            MaterialData *overrideMat = nullptr;
            if (meshIdx < current.overrideCount)
            {
                MaterialHandle h = current.overrideMaterials[meshIdx];
                if (h.IsValid()) overrideMat = m_services->resources->GetMaterial(h);
            }
            const MaterialData *activeMat = overrideMat
                                                ? overrideMat
                                                : (mesh.material && mesh.material->data ? mesh.material->data.get() : nullptr);

            if (!activeMat) continue; // マテリアルなし：スキップ

            // 透明判定：シェーダー PSO の blend 設定で判定（cbuffer 変数名に依存しない）
            const PipelineState *matPso        = shader->GetPSO();
            const bool           isTransparent = matPso && matPso->IsBlendEnabled();
            const bool           isSkinned     = !mesh.bones.empty();

            // このメッシュのローカル AABB（全インスタンスで使い回す）
            const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

            // =========================================================
            // 半透明：距離ソート用に後段へ転送（インスタンス単位でカリング）
            // =========================================================
            if (isTransparent)
            {
                for (size_t k = 0; k < batchCount; ++k)
                {
                    const auto &info = opaqueCommands[sortKeys[batchStart + k].originalIndex];

                    // スキンドはワールド行列がそのまま（骨で変形）、非スキンドは node 合成
                    const DirectX::XMFLOAT4X4 worldM =
                        isSkinned ? info.worldMatrix : MeshCull::ComputeMeshWorld(mesh, info.worldMatrix);

                    // 非スキンドのみカリング（スキンドは姿勢で AABB が変わるため除外）
                    if (!isSkinned && !MeshCull::IsMeshVisible(localAABB, worldM, frustum)) continue;

                    TransparencyCommand tInfo;
                    tInfo.shaderHash           = current.shaderHash;
                    tInfo.mesh                 = &mesh;
                    tInfo.customParams         = info.customParams;
                    tInfo.overrideMaterial     = (meshIdx < info.overrideCount)
                                                     ? info.overrideMaterials[meshIdx]
                                                     : MaterialHandle{};
                    tInfo.worldMatrix          = worldM;
                    const DirectX::XMVECTOR wp = DirectX::XMLoadFloat4x4(&tInfo.worldMatrix).r[3];
                    tInfo.distance             = DirectX::XMVectorGetX(
                        DirectX::XMVector3Length(DirectX::XMVectorSubtract(wp, cameraPos)));
                    queue->SubmitDeferredTransparent(tInfo);
                }
                continue; // 半透明はここで完了（不透明描画へは進まない）
            }

            // =========================================================
            // 不透明：インスタンスデータ構築（非スキンドはここでカリング）
            // =========================================================
            m_instanceScratch.clear();
            m_instanceScratch.reserve(batchCount);
            for (size_t k = 0; k < batchCount; ++k)
            {
                const auto  &cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                InstanceData data;
                data.customParams = cmd.customParams;

                if (isSkinned)
                {
                    // スキンドはカリングせず全数積む（ワールドは単位行列。変形は骨行列側）
                    DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
                    m_instanceScratch.push_back(data);
                }
                else
                {
                    data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
                    if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue; // 視界外は積まない
                    m_instanceScratch.push_back(data);
                }
            }

            // 可視インスタンスが 0 個なら、このメッシュを丸ごとスキップする。
            // ここが最重要：ドローコール・マテリアル更新・IA 設定・Map/Unmap を全て回避。
            if (m_instanceScratch.empty())
            {
                ++m_stat_culledMeshes;
                continue;
            }
            ++m_stat_drawnMeshes;

            if (isSkinned)
            {
                // override を matShader->Update に明示的に渡す（mesh.material を書き換えない）
                matShader->Update(dc, mesh, overrideMat);
                UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                // スキンドはカリングしていないため scratch.size() == batchCount
                for (size_t k = 0; k < m_instanceScratch.size(); ++k)
                {
                    const auto &cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    m_instanceBuffer.Write(dc, &m_instanceScratch[k], 1);
                    m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    renderer->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else
            {
                matShader->Update(dc, mesh, overrideMat);
                UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
                dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
                dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // batchCount ではなく「カリング後の可視インスタンス数」で描画する
                const size_t visibleCount = m_instanceScratch.size();
                size_t       curOffset    = 0;
                while (curOffset < visibleCount)
                {
                    const size_t drawCount = (std::min)(
                        visibleCount - curOffset, m_instanceBuffer.GetMaxInstances());
                    m_instanceBuffer.Write(dc, m_instanceScratch.data() + curOffset, drawCount);
                    m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()),
                        static_cast<UINT>(drawCount), 0, 0, 0);
                    curOffset += drawCount;
                }
            }
            m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        }
        matShader->End(dc, *m_services);
    }
}
