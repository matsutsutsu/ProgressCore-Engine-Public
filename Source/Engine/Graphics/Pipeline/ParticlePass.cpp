#include "ParticlePass.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"

void ParticlePass::Execute(const RenderFrameContext &ctx)
{

    if (m_services->particleRenderer)
    {
        m_services->particleRenderer->Render(*m_services, ctx);
    }
}