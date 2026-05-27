#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/AI/Navigation/Components/NavAgentComponent.h"
#include "Game/Logics/AI/NavAI/AIComponents.h"

// 徘徊ロジックを担当するシステム
class AIPatrolSystem : public CCL::ECS::IfSystem<AIPatrolSystem,
    CCL::ECS::Read<TransformComponent>,
    CCL::ECS::Write<AIStateComponent>,
    CCL::ECS::Write<NavAgentComponent>> {
public:
    AIPatrolSystem() : IfSystem("AIPatrolSystem") {}
    virtual ~AIPatrolSystem() = default;

    void Update(float dt) override;
};