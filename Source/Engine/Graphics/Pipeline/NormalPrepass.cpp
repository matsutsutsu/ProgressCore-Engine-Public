// NormalPrepass の実装。ShadowPass と同型の「専用シェーダー二度描き」パターン。
#include "NormalPrepass.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/RHI/Camera.h"
#include "Engine/Graphics/Renderer/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/RenderQueue.h"
#include "Engine/Graphics/Renderer/RenderDebugRegistry.h" // デバッグテクスチャ登録
#include "Engine/Graphics/Renderer/MeshCulling.h" // メッシュ単位カリング（OpaquePass と共有）
#include "Engine/Graphics/Shader/ShaderResources.h"
#include <DirectXCollision.h> // メッシュ単位フラスタムカリング用
#include <wrl/client.h>
#include <algorithm>

// [ cutout(アルファクリップ)マテリアルを足すときの注意 ]
//   このパスの共有 PS は clip() を行わない。つまり「テクスチャのアルファで
//   切り抜く」マテリアルをここに通すと、切り捨てられるはずの透明な部分にも
//   深度が書き込まれる。草の葉と葉の隙間や金網の網目から背景が見えるべき所が
//   塞がって穴になり、しかも OpaquePass 側の絵は正しいので原因が見えにくい。
//
//   以前は唯一の cutout だった Grass シェーダーをここで除外していたが、
//   Grass 自体を削除したため判定ごと外した。cutout を再導入する場合は、
//   そのマテリアルを本パスから除外して OpaquePass の自前描画に委ねること
//   （early-Z の恩恵は失うが、cutout では元々ほぼ効かない）。

void NormalPrepass::Initialize(const RenderServices& services)
{
    IRenderPass::Initialize(services);

    // Z プリパスを兼ねるため深度書き込みを有効化（DefaultOpaque = TestAndWrite）。
    // このパスが不透明ジオメトリの深度を先に確定させ、後続 OpaquePass は early-Z で
    // 隠れピクセルの重い PBR ピクセルシェーダをスキップできる。
    PipelineStateDesc desc = PipelineStateDesc::DefaultOpaque();
    m_pso = PipelineState(desc);

    m_instanceBuffer.Create(services.device, 8192);
    m_normalBuffer.Create(services.device, services.screenWidth, services.screenHeight);

    // 法線バッファの SRV は「生成された時点」で登録する。
    //   Graphics::MakeFrameContext はパイプライン実行の *前* に ctx を組み立てるので、
    //   Execute の末尾で登録していた頃は ctx.sceneNormalSRV が必ず1フレーム古かった
    //  （初回フレームは null）。SRV は生成時以外変わらないので、ここで渡しておけば
    //   初回から正しい値が入り、かつ「古い SRV を掴んだまま作り直される」窓も消える。
    PublishNormalSRV();
}

void NormalPrepass::OnResize(uint32_t width, uint32_t height)
{
    if (m_services && m_services->device) {
        m_normalBuffer.Resize(m_services->device, width, height);
        // Resize は SRV を作り直す。登録しなおさないと解放済みの SRV が残る。
        PublishNormalSRV();
    }
}

// 現在の法線バッファ SRV を Graphics へ預ける（MakeFrameContext が ctx へ渡す）。
void NormalPrepass::PublishNormalSRV()
{
    Graphics::Instance().SetSceneNormalSRV(m_normalBuffer.GetSRV());
}

void NormalPrepass::Execute(const RenderFrameContext& ctx)
{
    ID3D11DeviceContext* dc = ctx.dc;
    if (!dc) return;

    RenderQueue* queue = m_services->renderQueue;
    ModelConstantBuffers* modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // OpaquePass より前に実行されるため、ソート済みキーをここで構築する。
    // フレーム内で既に構築済みなら RenderQueue 側のガードで no-op になる（二重ソート防止）。
    queue->SortAndBuildBatches();

    Shader* shader = m_services->shaders->GetShader("NormalPrepass"_hash, m_services->device);
    if (!shader) return;

    // メッシュ単位フラスタムカリング用のワールド空間 frustum（OpaquePass と同一方式）。
    // camera は上流パイプラインが保証するが、念のため null なら法線書き出しをスキップする。
    if (!ctx.camera) return;
    const DirectX::BoundingFrustum& frustum = ctx.camera->GetFrustum();

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    m_normalBuffer.Activate(dc, ctx.targetDSV);
    m_pso.Apply(dc, m_services->renderState);

    modelConstants->BindAllGlobalConstantBuffers(dc);

    shader->Begin(dc, *m_services, ctx);
    dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const auto& sortKeys = queue->GetSortedKeys();
    const auto& opaqueCommands = queue->GetOpaqueCommands();

    size_t i = 0;
    while (i < sortKeys.size()) {
        const uint32_t currentIndex = sortKeys[i].originalIndex;
        const auto& current = opaqueCommands[currentIndex];

        const size_t   batchStart = i;
        size_t         batchCount = 0;
        const uint64_t currentBatchKey = sortKeys[batchStart].value;
        while (i < sortKeys.size() && sortKeys[i].value == currentBatchKey) { ++batchCount; ++i; }

        if (!current.model) continue;

        Shader* matShader = m_services->shaders->GetShader(current.shaderHash, m_services->device);
        if (matShader) {
            const PipelineState* matPso = matShader->GetPSO();
            if (matPso && matPso->IsBlendEnabled()) continue;
        }

        const auto& meshes = current.model->GetMeshes();
        for (size_t meshIdx = 0; meshIdx < meshes.size(); ++meshIdx) {
            const Model::Mesh& mesh = meshes[meshIdx];
            if (!mesh.data) continue;

            const bool isSkinned = !mesh.bones.empty();

            // このメッシュのローカル AABB（非スキンドのカリングで使い回す）。
            // OpaquePass と同一の MeshCull ヘルパを使い、両パスのカリング基準を一致させる。
            const DirectX::BoundingBox localAABB = MeshCull::BuildLocalAABB(mesh);

            m_instanceScratch.clear();
            m_instanceScratch.reserve(batchCount);
            for (size_t k = 0; k < batchCount; ++k) {
                const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                InstanceData data;
                data.customParams = cmd.customParams;
                if (isSkinned) {
                    // スキンドはカリングせず全数積む（変形は骨行列側。姿勢で AABB が変わるため安全側で除外）
                    DirectX::XMStoreFloat4x4(&data.world, DirectX::XMMatrixIdentity());
                    m_instanceScratch.push_back(data);
                }
                else {
                    data.world = MeshCull::ComputeMeshWorld(mesh, cmd.worldMatrix);
                    // OpaquePass と同じメッシュ単位フラスタムカリング。視界外は積まない。
                    if (!MeshCull::IsMeshVisible(localAABB, data.world, frustum)) continue;
                    m_instanceScratch.push_back(data);
                }
            }

            // このメッシュの可視インスタンス列を OpaquePass へ渡す。
            //   同じバッチ区切り・同じ MeshCull で走査しているので結果は完全に一致し、
            //   OpaquePass 側は行列合成とフラスタム判定を丸ごと省ける。
            //   空（全数カリング済み）も正当な結果なので、その事実ごと渡す。
            queue->StoreVisibleInstances(
                RenderQueue::MakeVisibleKey(batchStart, meshIdx),
                m_instanceScratch.data(), m_instanceScratch.size());

            // 可視インスタンスが 0 個なら、このメッシュの IA 設定・Map/Unmap・ドローを丸ごと回避する。
            if (m_instanceScratch.empty()) continue;

            UINT stride = sizeof(ModelResource::Vertex), offset = 0;
            dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
            dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

            if (isSkinned) {
                // スキンドはカリングしていないため m_instanceScratch.size() == batchCount
                for (size_t k = 0; k < m_instanceScratch.size(); ++k) {
                    const auto& cmd = opaqueCommands[sortKeys[batchStart + k].originalIndex];
                    m_instanceBuffer.Write(dc, &m_instanceScratch[k], 1);
                    m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);
                    modelConstants->UpdateSkeletonConstants(dc, cmd.model->GetMeshes()[meshIdx]);
                    dc->DrawIndexedInstanced(static_cast<UINT>(mesh.data->indices.size()), 1, 0, 0, 0);
                }
            }
            else {
                // batchCount ではなく「カリング後の可視インスタンス数」で描画する
                const size_t visibleCount = m_instanceScratch.size();
                size_t curOffset = 0;
                while (curOffset < visibleCount) {
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
    }

    shader->End(dc, *m_services);

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // 後続パスのシェーダーが t6 から読めるようにバインドする
    //（Graphics への登録は Initialize / OnResize 済み）。
    ID3D11ShaderResourceView* normalSRV = m_normalBuffer.GetSRV();
    dc->PSSetShaderResources(SLOT_SRV_SCENE_NORMAL, 1, &normalSRV);

    // デバッグ表示へ登録（RTV は上で解除済み。SRV として安全に読める）
    RenderDebugRegistry::Instance().Register({
        "NormalPrepass", normalSRV,
        m_normalBuffer.GetWidth(), m_normalBuffer.GetHeight(), DebugTexKind::Normal });
}
