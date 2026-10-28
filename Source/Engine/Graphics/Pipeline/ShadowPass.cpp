#include "ShadowPass.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/SceneView/Camera.h"
#include "Engine/Graphics/SceneView/Light.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Shared/ShaderShared.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Renderer/Core/RenderDebugRegistry.h"
#include "Engine/Graphics/Shader/Pass/ShadowUtils.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include "Engine/Graphics/Renderer/Core/MeshLod.h"
#include "tracy/Tracy.hpp"
#include "tracy/TracyD3D11.hpp"
#include <algorithm>

#if defined(TRACY_ENABLE)
namespace
{
    // Tracy は名前のポインタを持ち続けるので、どれもプログラム全体の寿命を持つ定数で渡す

    // カスケードごとの GPU ゾーン。
    // constexpr にすると MSVC 14.38 で C2131 になるので static const にする
    const tracy::SourceLocationData kCascadeGpuZones[3] = {
        {"GPU: Shadow Cascade 0", "ShadowPass::Execute", __FILE__, (uint32_t)__LINE__, 0},
        {"GPU: Shadow Cascade 1", "ShadowPass::Execute", __FILE__, (uint32_t)__LINE__, 0},
        {"GPU: Shadow Cascade 2", "ShadowPass::Execute", __FILE__, (uint32_t)__LINE__, 0},
    };

    // カスケードごとのグラフ
    const char *const kPlotInstances[3] = {"Shadow C0 Instances", "Shadow C1 Instances", "Shadow C2 Instances"};
    const char *const kPlotTriangles[3] = {"Shadow C0 Triangles", "Shadow C1 Triangles", "Shadow C2 Triangles"};
} // namespace
#endif



void ShadowPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoShadow = PipelineState(PipelineStateDesc::DefaultShadow());
    m_instanceBuffer.Create(services.device, 8192);
}



void ShadowPass::Execute(const RenderFrameContext &ctx)
{
    // 処理フロー:
    // 1. 描く準備（シェーダーとカスケードの行列）
    // 2. 影を落とすメッシュを集め、メッシュデータの順に並べる
    // 3. カスケードごとに、スキンメッシュを1体ずつ描き、それ以外をバッチで描く
    // 4. 描画先を戻し、デバッグ表示に登録する

    // 【備考】
    //   1 と 2 はどのカスケードでも同じなので、カスケードのループの外で1回だけ行う。

    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    const auto &shadowCommands = queue->GetShadowCommands();
    if (shadowCommands.empty()) return;

    ID3D11DeviceContext *dc        = ctx.dc;
    auto                &shadowMap = m_services->shadowMap;

    // ---------------------------------------------------------
    // 1. 描く準備
    // ---------------------------------------------------------

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> oldRTV;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> oldDSV;
    dc->OMGetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.GetAddressOf());
    UINT           numVP = 1;
    D3D11_VIEWPORT oldVP;
    dc->RSGetViewports(&numVP, &oldVP);

    Shader *shader = m_services->shaders->GetShader("ShadowMap"_hash, m_services->device);
    if (!shader) return;

    const DirectX::XMFLOAT3 lightDir =
        m_services->lightManager->GetDirectionalLight().direction;

    DirectX::XMMATRIX lightVPs[3];
    CalculateCascadeMatrices(
        XMLoadFloat4x4(&ctx.camera->GetView()),
        ctx.camera->GetFovY(),
        ctx.camera->GetAspect(),
        lightDir,
        shadowMap->params.cascadeSplits,
        lightVPs);

    // ---------------------------------------------------------
    // 2. メッシュを集めて並べる
    // ---------------------------------------------------------

    // 共有の mesh->data（頂点・インデックスバッファ）の順に並べる。
    // Model はエンティティごとに作られるので、Model::Mesh のアドレスで並べると
    // 同じモデルのドローンが1機ずつのバッチに分かれる
    {
        ZoneScopedN("Shadow: Collect & Sort");

        m_itemsScratch.clear();
        m_itemsScratch.reserve(shadowCommands.size());
        for (const auto &cmd : shadowCommands)
        {
            if (!cmd.mesh) continue;
            SortItem item;
            item.mesh        = cmd.mesh;
            item.cascadeMask = cmd.cascadeMask;
            if (!cmd.mesh->bones.empty() || !cmd.mesh->node)
            {
                item.worldMatrix = cmd.worldMatrix;
            }
            else
            {
                const DirectX::XMMATRIX ml = DirectX::XMLoadFloat4x4(&cmd.mesh->node->globalTransform);
                const DirectX::XMMATRIX ew = DirectX::XMLoadFloat4x4(&cmd.worldMatrix);
                DirectX::XMStoreFloat4x4(&item.worldMatrix, ml * ew);
            }
            m_itemsScratch.push_back(item);
        }
        std::sort(m_itemsScratch.begin(), m_itemsScratch.end(),
                  [](const SortItem &a, const SortItem &b) { return a.mesh->data < b.mesh->data; });
    }

    // ---------------------------------------------------------
    // 3. カスケードごとに描く
    // ---------------------------------------------------------

#if defined(TRACY_ENABLE)
    TracyD3D11Ctx tracyCtx = m_services->tracyContext; // GPU 計測のコンテキスト（無ければ GPU ゾーンを作らない）
#endif

    for (int cascadeIndex = 0; cascadeIndex < 3; ++cascadeIndex)
    {
        ZoneScopedN("Shadow: Cascade");
        ZoneValue(cascadeIndex);

#if defined(TRACY_ENABLE)
        // クリアも含めた、このカスケード全体の GPU 時間
        tracy::D3D11ZoneScope cascadeGpuZone(tracyCtx, &kCascadeGpuZones[cascadeIndex], tracyCtx != nullptr);
#endif

        int64_t instanceCount = 0; // このカスケードで描いたインスタンスの数（スキンメッシュは1体1つ）
        int64_t triangleCount = 0; // このカスケードで描いた三角形の数

        // --- 定数バッファ ---
        shadowMap->Activate(dc, cascadeIndex);
        m_psoShadow.Apply(dc, m_services->renderState);

        GpuData::CbShadow cbData{};
        XMStoreFloat4x4(&cbData.light_view_projection[0], lightVPs[0]);
        XMStoreFloat4x4(&cbData.light_view_projection[1], lightVPs[1]);
        XMStoreFloat4x4(&cbData.light_view_projection[2], lightVPs[2]);
        cbData.cascadeSplits = {shadowMap->params.cascadeSplits[0],
                                shadowMap->params.cascadeSplits[1],
                                shadowMap->params.cascadeSplits[2], 0.0f};

        cbData.shadow_color        = shadowMap->params.shadowColor;
        cbData.shadow_attenuation  = shadowMap->params.shadowAttenuation;
        cbData.shadow_bias         = shadowMap->params.shadowBias;
        cbData.currentCascadeIndex = cascadeIndex;
        shadowMap->UpdateConstantBuffer(dc, cbData);
        shadowMap->Bind(dc, -1, -1, SLOT_CB_SHADOW);

        shader->Begin(dc, *m_services, ctx);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // --- スキンメッシュの描画とバッチ作り ---
        const Model::Mesh *lastBound = nullptr; // 今 IA に設定しているメッシュ（スキンメッシュ用）

        // Batch は m_batchesScratch から使い回す（Batch::data の確保済みの容量も残る）
        m_batchCount      = 0;
        auto acquireBatch = [&]() -> Batch & {
            if (m_batchCount >= m_batchesScratch.size())
            {
                m_batchesScratch.emplace_back();
            }
            Batch &b   = m_batchesScratch[m_batchCount++];
            b.meshData = nullptr;
            b.data.clear();
            return b;
        };

        {
            ZoneScopedN("Shadow: Build Batches + Skinned");

            for (const auto &info : m_itemsScratch)
            {
                if (!info.mesh || !info.mesh->data) continue;
                if ((info.cascadeMask & (1 << cascadeIndex)) == 0) continue;

                // スキンメッシュはボーン行列が1体ごとに違うので、ここで1体ずつ描く
                const bool isSkinned = !info.mesh->bones.empty();
                if (isSkinned)
                {
                    modelConstants->UpdateSkeletonConstants(dc, *info.mesh);
                    if (lastBound != info.mesh)
                    {
                        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
                        dc->IASetVertexBuffers(0, 1, info.mesh->data->vertexBuffer.GetAddressOf(), &stride, &offset);
                        dc->IASetIndexBuffer(info.mesh->data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

                        lastBound = info.mesh;
                    }
                    dc->DrawIndexed(static_cast<UINT>(info.mesh->data->indices.size()), 0, 0);

                    instanceCount += 1;
                    triangleCount += static_cast<int64_t>(info.mesh->data->indices.size() / 3);
                    continue;
                }

                // それ以外は、同じメッシュデータが続く間は同じバッチに積む（2 で並べてあるため）
                if (m_batchCount > 0 && m_batchesScratch[m_batchCount - 1].meshData == info.mesh->data)
                {
                    m_batchesScratch[m_batchCount - 1].data.push_back({info.worldMatrix});
                }
                else
                {
                    Batch &b   = acquireBatch();
                    b.meshData = info.mesh->data;
                    b.data.push_back({info.worldMatrix});
                }
            }
        }

        // --- バッチの描画 ---

        // インスタンスの SRV はどのバッチでも同じなので、バインドはループの外で1回だけ
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        for (size_t bi = 0; bi < m_batchCount; ++bi)
        {
            const Batch &batch = m_batchesScratch[bi];

            // LOD のあるメッシュは一番粗い段で描く。影には形の細部がほとんど出ない。
            // 距離で段を選ばないので、カスケードやインスタンスごとに分けなくてよい
            const MeshLod::DrawRange range = MeshLod::GetCoarsestDrawRange(*batch.meshData);

            UINT stride = sizeof(ModelResource::Vertex), zero = 0;
            dc->IASetVertexBuffers(0, 1, batch.meshData->vertexBuffer.GetAddressOf(), &stride, &zero);
            dc->IASetIndexBuffer(range.indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            size_t total = batch.data.size(), offset = 0;
            while (offset < total)
            {
                const size_t n = (std::min)(total - offset, m_instanceBuffer.GetMaxInstances());
                m_instanceBuffer.Write(dc, batch.data.data() + offset, n);
                dc->DrawIndexedInstanced(range.indexCount, static_cast<UINT>(n), range.startIndex, 0, 0);

                instanceCount += static_cast<int64_t>(n);
                triangleCount += static_cast<int64_t>(range.indexCount / 3) * static_cast<int64_t>(n);
                offset += n;
            }
        }

        shader->End(dc, *m_services);
        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);

#if defined(TRACY_ENABLE)
        TracyPlot(kPlotInstances[cascadeIndex], instanceCount);
        TracyPlot(kPlotTriangles[cascadeIndex], triangleCount);
#endif
    }

    // ---------------------------------------------------------
    // 4. 後片付け
    // ---------------------------------------------------------

    dc->OMSetRenderTargets(1, oldRTV.GetAddressOf(), oldDSV.Get());
    dc->RSSetViewports(1, &oldVP);

    // デバッグ表示に登録する。
    // Texture2DArray なので、表示するスライスは RenderDebugWindow で選ぶ
    RenderDebugRegistry::Instance().Register({"ShadowMap (Cascades)", shadowMap->GetShaderResourceView(),
                                              shadowMap->GetWidth(), shadowMap->GetHeight(), DebugTexKind::Array});
}
