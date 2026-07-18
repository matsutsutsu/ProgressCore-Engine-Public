#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Game/Scenes/BaseScene.h"


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
   
    CCL::ECS::World* GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return _ecsContext->GetSystemManager(); }

};