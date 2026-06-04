#include "PostProcessPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void PostProcessPass::Execute(const RenderFrameContext& ctx)
{
    ZoneScopedN("Pass: PostProcess");

    if (!_services->postProcess) return;

    // HDR バッファをトーンマップして ctx.targetRTV（最終出力先）に書き出す。
    _services->postProcess->EndScene(ctx.dc, ctx.targetRTV);
}