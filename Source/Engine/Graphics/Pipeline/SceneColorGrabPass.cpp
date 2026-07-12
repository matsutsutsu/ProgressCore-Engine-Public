#include "SceneColorGrabPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SceneColorGrabPass::Execute(const RenderFrameContext& ctx)
{
    if (!_services || !_services->postProcess || !ctx.dc) return;

    _services->postProcess->GrabSceneColor(ctx.dc);
}
