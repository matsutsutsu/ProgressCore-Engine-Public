#include "GPUParticleRenderSystem.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void GPUParticleRenderSystem::Update(float dt)
{
    if (!m_world->HasResource<ParticleRenderer *>()) return;
    auto *renderer = m_world->GetResource<ParticleRenderer *>();

    ForEach([&](GPUParticleComponent &particle,
                const TransformComponent & /*transform*/) {
        // エミッタごとに1コマンド積む。
        //   ParticleRenderer が renderMode → テクスチャの順にソートするので、ここでは順番を気にしない。
        for (const ParticleEmitter &emitter : particle.emitters)
        {
            if (!emitter.isInitialized || !emitter.computeBuffer.IsValid()) continue;

            // インスペクタで非表示にした層は積まない。
            //   シミュレーション（R07）は止めないので、表示に戻すと続きから見える。
            //   保存しない値なので、エディタを含まないビルドでは常に true。
            if (!emitter.previewVisible) continue;

            ParticleDrawCommand cmd;
            cmd.computeBuffer = emitter.computeBuffer;
            cmd.texture       = emitter.texture;
            cmd.maxCount      = emitter.desc.maxCount;
            cmd.renderMode    = emitter.desc.renderMode;
            renderer->Draw(cmd);
        }
    });
}

void GPUParticleRenderSystem::ReleaseAllResources()
{
    ForEach([](GPUParticleComponent &particle, const TransformComponent &) {
        for (ParticleEmitter &emitter : particle.emitters)
        {
            if (!emitter.computeBuffer.IsValid()) continue;
            ResourceManager::Instance().UnloadParticleBuffer(emitter.computeBuffer);
            emitter.computeBuffer = ParticleBufferHandle{};
            emitter.isInitialized = false;
        }
    });
}

REGISTER_RENDER_SYSTEM(GPUParticleRenderSystem, Priority::RenderStage::R08_Main);