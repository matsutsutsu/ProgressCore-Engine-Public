#pragma once
// ===========================================================================
// 概要: キャラクターの速度を矢印で描くデバッグ表示 System。
//
// 責務:
//   - 青: CharacterMovementComponent::currentVelocity（物理へ渡した、壁に当たる前の速度）
//   - 赤: CharacterVirtual の実際の速度（地形との干渉を反映した結果）
//
// 備考: isDebugVisible が true のときだけ描く。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

class JoltCharacterDebugDrawSystem : public CCL::ECS::IfSystem<JoltCharacterDebugDrawSystem,
                                                               CCL::ECS::Read<TransformComponent>,
                                                               CCL::ECS::Read<CharacterMovementComponent>,
                                                               CCL::ECS::Read<JoltCharacterHandleComponent>>
{
public:
    JoltCharacterDebugDrawSystem() : IfSystem("JoltCharacterDebugDrawSystem") {}
    void Update(float dt) override;
};
