#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

// プレイヤーの意図（ロジック）と現実（Jolt物理）のベクトルを可視化するシステム
class JoltCharacterDebugDrawSystem : public CCL::ECS::IfSystem<JoltCharacterDebugDrawSystem,
                                         CCL::ECS::Read<TransformComponent>,
                                         CCL::ECS::Read<CharacterMovementInputComponent>,
                                         CCL::ECS::Read<JoltCharacterHandleComponent>>
{
  public:
    JoltCharacterDebugDrawSystem()
        : IfSystem("JoltCharacterDebugDrawSystem")
    {
        this->isDebugVisible = true;
    }
    void Update(float dt) override;
};