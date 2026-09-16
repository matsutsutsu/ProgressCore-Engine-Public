#pragma once
#include "Engine/Scene/BaseScene.h"

class ParticleScene : public BaseScene
{

public:
    ParticleScene() = default;
    ~ParticleScene() override {};

    void Initialize() override;
    void Finalize() override;

    CCL::ECS::World         *GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager *GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

protected:
    void RegisterSystems() override;
};