#pragma once
#include "Editor/Core/EditorWindowManager.h"
#include "Game/Scenes/BaseScene.h"


#include "Engine/GamePlay/Animation/AnimationSystem.h"

class EditorScene : public BaseScene {
  public:
    EditorScene() = default;
    ~EditorScene() override {};

    void Initialize() override;
    void Finalize() override;
    // Finalize, Update, Render, FixedUpdate は BaseScene をそのまま使うため削除！

    CCL::ECS::Core::World* GetWorld() override { return _worldPtr; }
    CCL::ECS::SystemManager* GetSystemManager() override { return _ecsContext->GetSystemManager(); }
  protected:
    void RegisterSystems() override;

    // BaseSceneのUpdateをオーバーライドする
    void FixedUpdate(float dt) override;
    void Update(float dt) override;

};