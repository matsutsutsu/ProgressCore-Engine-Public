#include "SetupPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SetupPass::Execute(const RenderFrameContext& ctx)
{

    // MakeLegacyContext 不要: m_services / ctx を直接参照
    if (!m_services->postProcess) return;

    float clearColor[] = { 0.11f, 0.16f, 0.25f, 1.0f };
    m_services->postProcess->BeginScene(ctx.dc, clearColor, ctx.targetDSV);
}