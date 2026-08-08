// ===========================================================================
// GPUParticleRenderSystem.cpp
// ---------------------------
// 役割・責務はヘッダを参照。描画コマンドを ParticleRenderer のバケツへ積むだけ。
//
// 実装の意図(Why):
//   - 描画可否は「GPUバッファが用意できているか」だけで判定する。
//   - かつて parent==0(ルート)のエミッタを描画対象外にしていたが、VFXSystem が
//     ワールド座標へ置くVFXの root は当然 parent==0 になるため、ルートに直接
//     エミッタを持つプレハブが無言で描画されない罠になっていた。よって撤去。
// ===========================================================================
#include "GPUParticleRenderSystem.h"
#include "Engine/Graphics/RHI/Graphics.h"
#include "Engine/Graphics/Renderer/ParticleRenderer.h"
#include "Engine/Graphics/Resource/ResourceManager.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

void GPUParticleRenderSystem::Update(float dt)
{
    if (!m_world->HasResource<ParticleRenderer*>()) return;
    auto* renderer = m_world->GetResource<ParticleRenderer*>();

    ForEach([&](GPUParticleComponent& particle,
        const TransformComponent& /*transform*/) {

            // エフェクトを構成するエミッタごとに1コマンド積む。
            //   ParticleRenderer 側が renderMode → テクスチャ順にソートするので、
            //   ここでは順序を気にせず積んでよい。
            for (const ParticleEmitter& emitter : particle.emitters)
            {
                if (!emitter.isInitialized || !emitter.computeBuffer.IsValid()) continue;

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
    ForEach([](GPUParticleComponent& particle, const TransformComponent&) {
        for (ParticleEmitter& emitter : particle.emitters) {
            if (!emitter.computeBuffer.IsValid()) continue;
            ResourceManager::Instance().UnloadParticleBuffer(emitter.computeBuffer);
            emitter.computeBuffer  = ParticleBufferHandle{};
            emitter.isInitialized  = false;
        }
        });
}

REGISTER_RENDER_SYSTEM(GPUParticleRenderSystem, Priority::RenderStage::R08_Main);