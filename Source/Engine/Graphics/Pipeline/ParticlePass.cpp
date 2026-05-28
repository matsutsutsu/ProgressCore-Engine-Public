#include "ParticlePass.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"

void ParticlePass::Execute(const RenderFrameContext& ctx)
{
#if TRACY_ENABLE
    ZoneScopedN("Pass: Particle");
#endif

    RenderContext rc = MakeLegacyContext(*_services, ctx);

    if (rc.particleRenderer) {
        rc.particleRenderer->Render(rc.deviceContext);
    }
}