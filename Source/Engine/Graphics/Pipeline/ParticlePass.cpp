#include "ParticlePass.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"

void ParticlePass::Execute(const RenderFrameContext& ctx)
{
    ZoneScopedN("Pass: Particle");

    if (_services->particleRenderer) {
        _services->particleRenderer->Render(*_services,ctx);
    }
}