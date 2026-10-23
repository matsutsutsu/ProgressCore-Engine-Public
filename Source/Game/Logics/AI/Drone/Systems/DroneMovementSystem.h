#pragma once
// ===========================================================================
// 概要: ドローンを目標座標へ移動・回転させる System（L03_PrePhysics）。
//
// 責務:
//   - currentState ごとに速度（currentVelocity）と姿勢を決める
//   - 速度を位置へ積分し、床より下へ潜らせない
//
// 備考:
//   Idle・MoveToTarget・FireCharge の動きは DroneMotion にある。
//   ここに残すのは、どの技にも共通の Returning と、積分・床・姿勢の確定。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/AI/Drone/Components/DroneComponent.h"
#include "Game/Logics/AI/Drone/Motion/DroneMotion.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Time/Components/TimeState.h"

class DroneMovementSystem : public CCL::ECS::IfSystem<
                                DroneMovementSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<DroneComponent>,
                                CCL::ECS::Read<TimeStateComponent>>
{
public:
    DroneMovementSystem() : IfSystem("DroneMovementSystem") {}

    void Update(float dt) override;

private:
    // 突き抜けた後、上へ抜けながら減速する（離脱）。その後の復帰は MoveToTarget に任せる
    void ProcessReturning(TransformComponent &trans, DroneComponent &drone, float dt);
};
