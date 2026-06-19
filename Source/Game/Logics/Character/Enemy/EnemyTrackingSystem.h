#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "EnemyComponent.h"
#include "../Player/PlayerComponent.h" // パスは適宜合わせてください

// ★修正: Transform を直接書き換えるため Write<TransformComponent> に変更
class EnemyTrackingSystem : public CCL::ECS::IfSystem<
    EnemyTrackingSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<EnemyComponent>>
{
public:
    EnemyTrackingSystem();
    void Update(float dt) override;
};