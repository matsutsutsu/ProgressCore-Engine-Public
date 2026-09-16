#include "PrimitivePass.h"
#include "Engine/Graphics/Renderer/PrimitiveRenderer.h"
#include "Engine/Graphics/Renderer/ShapeRenderer.h"
#include "Engine/Graphics/RHI/Camera.h"

void PrimitivePass::Execute(const RenderFrameContext &ctx)
{

    if (!ctx.camera) return;

    if (m_services->shapeRenderer)
    {
        m_services->shapeRenderer->Render(
            ctx.dc, ctx.camera->GetView(), ctx.camera->GetProjection());
    }

    // PrimitiveRenderer が新 API に移行したため MakeLegacyContext 不要。
    // RenderContext 依存がこのパスから完全に消滅した。
    if (m_services->primitiveRenderer)
    {
        m_services->primitiveRenderer->Render(
            ctx.dc, *m_services,
            ctx.camera->GetView(), ctx.camera->GetProjection());
    }
}