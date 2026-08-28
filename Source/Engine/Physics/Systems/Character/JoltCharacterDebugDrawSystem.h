#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

// プレイヤーの意図（ロジック）と現実（Jolt物理）のベクトルを可視化するシステム
class JoltCharacterDebugDrawSystem : public CCL::ECS::IfSystem<JoltCharacterDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<CharacterMovementComponent>,
                                         CCL::ECS::Read<JoltCharacterHandleComponent>>
{
  public:
    JoltCharacterDebugDrawSystem()
        : IfSystem("JoltCharacterDebugDrawSystem")
    {
        //this->isDebugVisible = true;
    }
    void Update(float dt) override;
};
