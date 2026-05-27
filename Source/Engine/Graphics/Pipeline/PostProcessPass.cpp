#include "PostProcessPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"


void PostProcessPass::Execute(const RenderContext& rc){

#if TRACY_ENABLE
    ZoneScopedN("Pass: PostProcess");
#endif

    if (!rc.postProcess) return;

    // HDRバッファにブルームとトーンマップをかけ、バックバッファ（画面）に出力する
    rc.postProcess->EndScene(rc.deviceContext, rc.targetRTV);
}