#include "SetupPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void SetupPass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Setup");
#endif

    RenderContext rc = MakeLegacyContext(*_services, ctx);
    if (!rc.postProcess) return;

    float clearColor[] = { 0.11f, 0.16f, 0.25f, 1.0f };

    // Graphics::Instance().GetDepthStencilView() → ctx.targetDSV に変更。
    // targetDSV はフレームコンテキストに含まれるため、シングルトンアクセス不要。
    rc.postProcess->BeginScene(ctx.dc, clearColor, ctx.targetDSV);
}