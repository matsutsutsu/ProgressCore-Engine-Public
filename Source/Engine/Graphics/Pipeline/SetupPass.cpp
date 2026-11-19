#include "SetupPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SetupPass::Execute(const RenderFrameContext &ctx)
{
    if (!m_services->postProcess) return;

    // HDR バッファをこの色でクリアし、シーンの深度と組にして描画先にする
    // 空が描かれないフレーム（シーンの最初など）は、この色が背景に出る
    float clearColor[] = {0.11f, 0.16f, 0.25f, 1.0f};
    m_services->postProcess->BeginScene(ctx.dc, clearColor, ctx.targetDSV);
}