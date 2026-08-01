#pragma once
#include "Engine/Scene/BaseScene.h"

class ParticleScene : public BaseScene {

  public:
    ParticleScene() = default;
    ~ParticleScene() override {};

    void Initialize() override;
    void Finalize() override;

    CCL::ECS::World* GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return _ecsContext->GetSystemManager(); }
  protected:
    void RegisterSystems() override;
};