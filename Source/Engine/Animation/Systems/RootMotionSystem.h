#pragma once
// ===========================================================================
// 概要: アニメーションのルート移動量をキャラクターの移動速度に変換する System（L02_Update）。
//
// 責務:
//   - ステートが applyRootMotion のとき、ルートモーションカーブ（あれば）かルートボーンの移動量から
//     このステップの前進量を求め、CharacterMovementComponent.rootMotionVelocity に書く
//   - 使わないステップでは出力を必ず無効にする
//
// 備考: ダッシュ等の個別の仕組みは知らない。外部が移動を制御中かどうかは
//       RootMotionComponent.externallyDriven（ゲーム側が書く）で受け取る。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Animation/Components/AnimatorComponent.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Animation/Components/RootMotionComponent.h"
#include "Engine/Graphics/Components/Core/ModelComponent.h"

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

    /**
     * @brief 同じステージで先に走らせる System を宣言する。
     *
     * AnimControllerSystem が決めた遷移の結果に対して適用する。
     * PlayerDashSystem が externallyDriven を書いてから読む必要があるので、その後に走らせる。
     */
    std::vector<const char *> GetRunAfter() const override
    {
        return {"AnimControllerSystem", "PlayerDashSystem"};
    }

    void Update(float dt) override;
};