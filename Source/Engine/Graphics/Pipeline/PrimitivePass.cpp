#include "PrimitivePass.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/RHI/Camera.h"

void PrimitivePass::Execute(const RenderFrameContext& ctx)
{ 
    ZoneScopedN("Pass: Primitive");

    if (!ctx.camera) return;

    if (_services->shapeRenderer) {
        _services->shapeRenderer->Render(
            ctx.dc, ctx.camera->GetView(), ctx.camera->GetProjection());
    }

    // PrimitiveRenderer が新 API に移行したため MakeLegacyContext 不要。
      // RenderContext 依存がこのパスから完全に消滅した。
    if (_services->primitiveRenderer) {
        _services->primitiveRenderer->Render(
            ctx.dc, *_services,
            ctx.camera->GetView(), ctx.camera->GetProjection());
    }
}