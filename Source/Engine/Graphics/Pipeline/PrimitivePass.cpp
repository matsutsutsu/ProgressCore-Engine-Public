#include "PrimitivePass.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/RHI/Camera.h"

void PrimitivePass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Primitive");
#endif
    if (!ctx.camera) return;

    if (_services->shapeRenderer) {
        _services->shapeRenderer->Render(
            ctx.dc, ctx.camera->GetView(), ctx.camera->GetProjection());
    }

    // PrimitiveRenderer::Render がまだ RenderContext を受け取る旧 API のため、
    // ここだけ MakeLegacyContext を使用する。
    // PrimitiveRenderer 側を (dc, view, proj) 形式に更新したら削除する。
    if (_services->primitiveRenderer) {
        RenderContext legacyCtx = MakeLegacyContext(*_services, ctx);
        _services->primitiveRenderer->Render(
            legacyCtx, ctx.camera->GetView(), ctx.camera->GetProjection());
    }
}