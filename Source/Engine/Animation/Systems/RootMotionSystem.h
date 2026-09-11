#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

// このシステムは特定ゲーム機構（ダッシュ等）を一切知らない汎用システム。
//   「外部が移動を制御中だから root motion を止めたい」という判断は
//   RootMotionComponent.externallyDriven（ゲーム側の移動アクションが設定）で受け取る。
class RootMotionSystem : public CCL::ECS::IfSystem<RootMotionSystem,
                             CCL::ECS::Write<RootMotionComponent>,
                             CCL::ECS::Write<CharacterMovementComponent>,
                             CCL::ECS::Read<AnimatorComponent>,
                             CCL::ECS::Read<AnimStateMachineComponent>,
                             CCL::ECS::Read<TransformComponent>,
                             CCL::ECS::Read<TimeStateComponent>,
                             CCL::ECS::Read<ModelComponent>>
{
  public:
    RootMotionSystem()
        : IfSystem("RootMotionSystem")
    {
    }

    // 実行順序の宣言（TaskScheduler が同一ステージ内の順序決定に使う）。
    // AnimController が決めた遷移結果に対してルートモーションを適用する。
    //
    // PlayerDashSystem と同一ステージで CharacterMovement を奪い合う。
    // ダッシュ側が rm.externallyDriven を書いてから読む必要があるため後に走らせる。
    std::vector<const char *> GetRunAfter() const override
    {
        return {"AnimControllerSystem", "PlayerDashSystem"};
    }

    void Update(float dt) override;
};