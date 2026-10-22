#include "TrailPass.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

void TrailPass::Execute(const RenderFrameContext &ctx)
{
    if (!m_services || !m_services->trailRenderer)
    {
        return;
    }

    // TrailSystem（R08）がこのフレームに積んだ頂点を、歪み・加算の順に描いて空にする
    m_services->trailRenderer->Render(*m_services, ctx);
}
