#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "Engine/GamePlay/Physics/Jolt/JoltHandleComponent.h"
#include "EnemyComponent.h"

// プレイヤーを追従するAIシステム
class EnemyMoveSystem : public CCL::ECS::IfSystem<EnemyMoveSystem,
                            CCL::ECS::Read<TransformComponent>,
                            CCL::ECS::Write<EnemyComponent>,
                            CCL::ECS::Read<JoltHandleComponent>> {
public:
    EnemyMoveSystem() : IfSystem("EnemyMoveSystem") {}
    void Update(float dt) override;
};