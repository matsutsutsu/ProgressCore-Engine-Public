#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Graphics/Core/ModelComponent.h"
#include "Engine/GamePlay/Animation/AnimatorComponent.h"

// 名前空間はプロジェクトに合わせて調整してください
class AnimationSystem : public CCL::ECS::IfSystem<AnimationSystem,
                            CCL::ECS::Write<AnimatorComponent>,
                            CCL::ECS::Write<ModelComponent>> {
  public:
    AnimationSystem();
    virtual ~AnimationSystem() = default;

    void Update(float dt) override;

    // エディタ用の手動更新関数
    void UpdateManual(CCL::ECS::EntityID entity, float time);
};