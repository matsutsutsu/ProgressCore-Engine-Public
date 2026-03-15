#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Engine/GamePlay/Graphics/Particle/GPUParticleRenderSystem.h"
#include "Game/Scenes/BaseScene.h"

class ParticleScene : public BaseScene {
    std::unique_ptr<EditorWindowManager>     _windowManager;
    std::shared_ptr<GPUParticleRenderSystem> _particleRenderer;
    CCL::ECS::EntityID                       selectedEntity = 0;

  public:
    ParticleScene() = default;
    ~ParticleScene() override {};

    void Initialize() override;
    void Finalize() override;

  protected:
    void RegisterSystems() override;
    void OnRender3D(RenderContext &rc) override;
    void OnDrawImGui() override;
    void OnPreLoadScene() override;
};