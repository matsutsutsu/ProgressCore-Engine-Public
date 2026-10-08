#pragma once
// ===========================================================================
// 概要: 経路（waypoints）をたどるように、キャラクターの移動速度と向きを決める System。
//
// 責務:
//   - 今の waypoint への水平方向の速度を CharacterMovementComponent へ書く（移動は物理が行う）
//   - 進行方向へ向きを滑らかに回す
//   - waypoint に着いたら次へ進める
//
// 備考: 現在は System として登録していない（.cpp 末尾の TODO 参照）。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"

class NavSteeringSystem : public CCL::ECS::IfSystem<NavSteeringSystem,
                                                    CCL::ECS::Write<TransformComponent>,
                                                    CCL::ECS::Write<NavAgentComponent>,
                                                    CCL::ECS::Write<CharacterMovementComponent>>
{
public:
    NavSteeringSystem() : IfSystem("NavSteeringSystem") {}
    virtual ~NavSteeringSystem() = default;

    void Update(float dt) override;
};