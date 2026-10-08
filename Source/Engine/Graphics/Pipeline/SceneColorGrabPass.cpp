#include "SceneColorGrabPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SceneColorGrabPass::Execute(const RenderFrameContext &ctx)
{
    if (!m_services || !m_services->postProcess || !ctx.dc) return;

    m_services->postProcess->GrabSceneColor(ctx.dc);
}
