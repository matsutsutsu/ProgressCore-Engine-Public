#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Graphics/Components/Particle/GPUParticleComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

// 描画コマンドを収集して ParticleRenderer のバケツへ積むだけの軽量システム。
class GPUParticleRenderSystem : public CCL::ECS::IfSystem<GPUParticleRenderSystem,
    CCL::ECS::Write<GPUParticleComponent>,
    CCL::ECS::Read<TransformComponent>> {
public:
    GPUParticleRenderSystem() : IfSystem("GPUParticleRenderSystem") {}
    ~GPUParticleRenderSystem() { ReleaseAllResources(); }

    void Update(float dt) override;
    void ReleaseAllResources();
};