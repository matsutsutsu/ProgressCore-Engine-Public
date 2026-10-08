#include "TrailPass.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

void TrailPass::Execute(const RenderFrameContext &ctx)
{
    if (!m_services) return;
    // TrailSystem（R08）がこのフレームに積んだ頂点を、歪み・加算の順に描いて空にする
    TrailRenderer::Instance().Render(*m_services, ctx);
}
