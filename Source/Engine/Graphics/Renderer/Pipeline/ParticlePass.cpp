#include "ParticlePass.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"

void ParticlePass::Execute(const RenderContext& rc){
#ifdef TRACY_ENABLE
    ZoneScopedN("Pass: Particle");
#endif

    if (rc.particleRenderer) {
        rc.particleRenderer->Render(rc.deviceContext);
    }
}