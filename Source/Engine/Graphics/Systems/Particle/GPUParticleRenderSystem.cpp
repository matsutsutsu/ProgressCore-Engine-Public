#include "GPUParticleRenderSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Game/Core/SystemPriority.h"

void GPUParticleRenderSystem::Update(float dt)
{
    if (!_world->HasResource<ParticleRenderer*>()) return;
    auto* renderer = _world->GetResource<ParticleRenderer*>();

    ForEach([&](GPUParticleComponent& particle, const TransformComponent& transform) {
        if (transform.parent == 0)                                return;
        if (!particle.isInitialized || !particle.computeBuffer.IsValid()) return;

        ParticleDrawCommand cmd;
        cmd.computeBuffer = particle.computeBuffer;
        cmd.texture = particle.texture;
        cmd.maxCount = particle.desc.maxCount;    // 旧: config.max_particle_count
        cmd.renderMode = particle.desc.renderMode;  // 旧: config.render_mode
        renderer->Draw(cmd);
        });
}

void GPUParticleRenderSystem::ReleaseAllResources()
{
    ForEach([](GPUParticleComponent& particle, const TransformComponent&) {
        if (particle.computeBuffer.IsValid()) {
            ResourceManager::Instance().UnloadParticleBuffer(particle.computeBuffer);
            particle.computeBuffer = ParticleBufferHandle{};
            particle.isInitialized = false;
        }
        });
}

REGISTER_RENDER_SYSTEM(GPUParticleRenderSystem, Priority::RenderStage::R08_Main);