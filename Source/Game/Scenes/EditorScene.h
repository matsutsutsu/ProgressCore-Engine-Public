#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Engine/Scene/BaseScene.h"


#include "Engine/Animation/Systems/AnimationSystem.h"

class EditorScene : public BaseScene {
  public:
    EditorScene() = default;
    ~EditorScene() override {};

    void Initialize() override;
    void Finalize() override;
    void FixedUpdate(float dt) override;
    void Update(float dt) override;
    void Render() override;

    void RegisterSystems() override;
   
    CCL::ECS::World* GetWorld() override { return m_worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return m_ecsContext->GetSystemManager(); }

};