#include "SetupPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SetupPass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Setup");
#endif
    // MakeLegacyContext 不要: _services / ctx を直接参照
    if (!_services->postProcess) return;

    float clearColor[] = { 0.11f, 0.16f, 0.25f, 1.0f };
    _services->postProcess->BeginScene(ctx.dc, clearColor, ctx.targetDSV);
}