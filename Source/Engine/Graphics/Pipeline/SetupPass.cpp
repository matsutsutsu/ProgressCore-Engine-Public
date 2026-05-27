#include "SetupPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"
#include "Engine/Graphics/RHI/Graphics.h"

void SetupPass::Execute(const RenderContext& rc){
#if TRACY_ENABLE
    ZoneScopedN("Pass: Setup");
#endif

    if (!rc.postProcess) return;

    // 背景色（BaseSceneにあったもの）
    float clearColor[] = { 0.11f, 0.16f, 0.25f, 1.0f };

    // HDRバッファのバインドとクリア
    rc.postProcess->BeginScene(rc.deviceContext, clearColor, Graphics::Instance().GetDepthStencilView());
}