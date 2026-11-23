#include "TransparentPass.h"
#include "Engine/Graphics/Renderer/Core/ModelConstantBuffers.h"
#include "Engine/Graphics/Shader/ShaderRegistry.h"
#include "Engine/Graphics/Renderer/Core/RenderQueue.h"
#include "Engine/Graphics/Shader/ShaderResources.h"
#include "Engine/Graphics/Shader/Pass/ShadowMap.h"
#include <algorithm>

void TransparentPass::Initialize(const RenderServices &services)
{
    IRenderPass::Initialize(services);
    m_psoTransparent = PipelineState(PipelineStateDesc::DefaultTransparent());
    m_instanceBuffer.Create(services.device, 8192);
}



void TransparentPass::Execute(const RenderFrameContext &ctx)
{
    // 処理フロー:
    // 1. 描く準備（シャドウマップと半透明用の PSO）
    // 2. 半透明のコマンドを、カメラから遠い順に並べる
    // 3. 1メッシュずつ、インスタンスの値とマテリアルを入れて描く

    ID3D11DeviceContext  *dc             = ctx.dc;
    RenderQueue          *queue          = m_services->renderQueue;
    ModelConstantBuffers *modelConstants = m_services->modelConstants;
    if (!queue || !modelConstants) return;

    // ---------------------------------------------------------
    // 1. 描く準備
    // ---------------------------------------------------------

    // マテリアルのシェーダーが影を計算できるよう、シャドウマップをバインドする
    if (m_services->shadowMap)
    {
        m_services->shadowMap->Bind(dc, SLOT_SRV_SHADOW, SLOT_SMP_SHADOW, SLOT_CB_SHADOW);
    }

    // ブレンドあり・深度はテストだけで書かない
    // ここに来るのは alphaMode が Blend のメッシュだけなので、全部この PSO で描く
    m_psoTransparent.Apply(dc, m_services->renderState);

    // ---------------------------------------------------------
    // 2. 遠い順に並べる
    // ---------------------------------------------------------

    // OpaquePass が回した半透明のコマンドを、その場で並べ替える
    // この配列は毎フレーム作り直すので、並べ替えて壊してかまわない
    auto &allTrans = queue->GetDeferredTransparentCommandsMutable();
    std::sort(allTrans.begin(), allTrans.end(),
              [](const TransparencyCommand &l, const TransparencyCommand &r) {
                  return l.distance > r.distance;
              });

    // ---------------------------------------------------------
    // 3. 1メッシュずつ描く
    // ---------------------------------------------------------

    for (const TransparencyCommand &info : allTrans)
    {
        Shader          *shader    = m_services->shaders->GetShader(info.shaderHash, m_services->device);
        IMaterialShader *matShader = shader ? shader->AsMaterial() : nullptr;
        if (!matShader) continue;

        // シェーダーと入力レイアウトを設定する
        matShader->Begin(dc, *m_services, ctx);

        const Model::Mesh &mesh = *info.mesh;

        // 上書きのマテリアルは引数で渡す。mesh.material に書くと、上書きをやめた後のフレームにも残るので書き換えない
        MaterialData *overrideMat = nullptr;
        if (info.overrideMaterial.IsValid())
        {
            overrideMat = m_services->resources->GetMaterial(info.overrideMaterial);
        }

        // --- インスタンス1体分の値 ---

        // スキンメッシュは、ワールド行列がボーン行列に入っているので単位行列にする
        const bool   isSkinned = !mesh.bones.empty();
        InstanceData singleData;
        singleData.customParams = info.customParams;
        if (isSkinned)
        {
            DirectX::XMStoreFloat4x4(&singleData.world, DirectX::XMMatrixIdentity());
        }
        else
        {
            singleData.world = info.worldMatrix;
        }

        m_instanceBuffer.Write(dc, &singleData, 1);
        m_instanceBuffer.Bind(dc, SLOT_SRV_INSTANCE);

        // スキンメッシュは、この1体のボーン行列を CbSkeleton に書く
        if (isSkinned) modelConstants->UpdateSkeletonConstants(dc, mesh);

        // このメッシュのマテリアルの値を定数バッファに書き、テクスチャをバインドする
        matShader->Update(dc, mesh, overrideMat);

        // --- 描画 ---

        UINT stride = sizeof(ModelResource::Vertex), offset = 0;
        dc->IASetVertexBuffers(0, 1, mesh.data->vertexBuffer.GetAddressOf(), &stride, &offset);
        dc->IASetIndexBuffer(mesh.data->indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        dc->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        dc->DrawIndexed(static_cast<UINT>(mesh.data->indices.size()), 0, 0);

        m_instanceBuffer.Unbind(dc, SLOT_SRV_INSTANCE);
        matShader->End(dc, *m_services);
    }
}
