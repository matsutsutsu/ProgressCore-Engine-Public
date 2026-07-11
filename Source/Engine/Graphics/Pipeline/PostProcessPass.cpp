#include "PostProcessPass.h"
#include "Engine/Graphics/Shader/PostProcess/PostProcessManager.h"
#include "Engine/Graphics/Shader/ShaderResources.h"

void PostProcessPass::Execute(const RenderFrameContext& ctx)
{

    if (!_services->postProcess) return;

    if (_services->sceneConstantBuffer) {
        ID3D11Buffer* sceneCB = _services->sceneConstantBuffer;
        ctx.dc->PSSetConstantBuffers(SLOT_CB_SCENE, 1, &sceneCB);
    }

    _services->postProcess->EndScene(ctx.dc, ctx.targetRTV, ctx.sceneDepthSRV);
}