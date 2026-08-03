#include "TrailPass.h"
#include "Engine/Graphics/Renderer/TrailRenderer.h"

void TrailPass::Execute(const RenderFrameContext& ctx)
{
    if (!m_services) return;
    // TrailSystem(R08)がこのフレームに積んだ頂点を描画してクリアする。
    TrailRenderer::Instance().Render(*m_services, ctx);
}
