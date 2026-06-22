#pragma once
/// @file PlayerMoveSystem.h
/// @brief 意思データを物理エンジン(Jolt CharacterVirtual)の速度情報へ変換し流し込むシステム。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterConfigComponent.h"
#include "Engine/Physics/Components/Character/JoltCharacterHandleComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<
    PlayerMoveSystem,
    CCL::ECS::Write<TransformComponent>, // ★モデルの回転(Yaw)反映のためWriteを維持
    CCL::ECS::Read<CharacterMovementInputComponent>,
    CCL::ECS::Read<JoltCharacterConfigComponent>,
    CCL::ECS::Read<JoltCharacterHandleComponent>, // ★Jolt実体へのアクセス
    CCL::ECS::Read<PlayerComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}
    virtual ~PlayerMoveSystem() = default;

    void Update(float dt) override;
};