#include "PostProcessPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

void PostProcessPass::Execute(const RenderFrameContext &ctx)
{
    if (!m_services->postProcess) return;

    // エフェクトのシェーダーがカメラの行列や画面サイズを読めるよう、CbScene をバインドする
    if (m_services->sceneConstantBuffer)
    {
        ID3D11Buffer *sceneCB = m_services->sceneConstantBuffer;
        ctx.dc->VSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneCB);
        ctx.dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneCB);
    }

    // HDR バッファにエフェクトとトーンマップをかけ、ctx.targetRTV に書き出す
    m_services->postProcess->EndScene(
        ctx.dc, ctx.targetRTV, ctx.sceneDepthSRV, ctx.sceneNormalSRV);
}