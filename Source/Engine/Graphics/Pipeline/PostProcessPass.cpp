#include "PostProcessPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"

void PostProcessPass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: PostProcess");
#endif

    RenderContext rc = MakeLegacyContext(*_services, ctx);
    if (!rc.postProcess) return;

    // HDR バッファにブルームとトーンマップをかけ、最終出力先（ctx.targetRTV）に書き出す。
    // targetRTV はフレームコンテキストから取得するためシングルトンアクセス不要。
    rc.postProcess->EndScene(rc.deviceContext, ctx.targetRTV);
}