#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Game/Logics/Character/Enemy/MoveIntentComponent.h"
#include "Engine/Timer/TimeState.h" // ★ 追加

class EnemyMovementSystem : public CCL::ECS::IfSystem<
    EnemyMovementSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<MoveIntentComponent>,
    CCL::ECS::Read<TimeStateComponent>> // ★ 追加
{
public:
    EnemyMovementSystem();
    void Update(float dt) override;
};