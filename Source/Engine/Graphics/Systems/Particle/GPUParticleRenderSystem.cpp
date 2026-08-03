// ===========================================================================
// GPUParticleRenderSystem.cpp
// ---------------------------
// 役割・責務はヘッダを参照。描画コマンドを ParticleRenderer のバケツへ積むだけ。
//
// 実装の意図(Why):
//   - parent==0(ルート)のエミッタは描画しない既存仕様を維持(子として接がれた
//     インスタンスのみ描画する設計)。
//   - プール休眠中(isSleeping)のエミッタは描画もスキップする。休眠インスタンスは
//     親なしで free-list に待機しているため、ここで弾く(Update側のスキップと対)。
// ===========================================================================
#include "GPUParticleRenderSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/Graphics/Components/Particle/PooledParticleComponent.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void GPUParticleRenderSystem::Update(float dt)
{
    if (!m_world->HasResource<ParticleRenderer*>()) return;
    auto* renderer = m_world->GetResource<ParticleRenderer*>();

    ForEachWithID([&](CCL::ECS::EntityID id,
        GPUParticleComponent& particle,
        const TransformComponent& transform) {

            if (transform.parent == 0)                                         return;
            if (!particle.isInitialized || !particle.computeBuffer.IsValid())  return;

            // プール休眠中は描画しない（Update側のスキップと対になる）。
            if (auto* pooled = m_world->GetComponent<PooledParticleComponent>(id))
                if (pooled->isSleeping) return;

            ParticleDrawCommand cmd;
            cmd.computeBuffer = particle.computeBuffer;
            cmd.texture = particle.texture;
            cmd.maxCount = particle.desc.maxCount;
            cmd.renderMode = particle.desc.renderMode;
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