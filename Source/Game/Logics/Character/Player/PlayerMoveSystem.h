#pragma once
/// @file PlayerMoveSystem.h
/// @brief MovementInputComponent の「意思データ」を読み取り、実際の Transform (座標・回転) に反映させるシステム。
/// @note 入力デバイスへのアクセスは禁止。純粋な数学的データ変換器として機能する。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "Engine/Timer/TimeState.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"

class PlayerMoveSystem : public CCL::ECS::IfSystem<
    PlayerMoveSystem,
    CCL::ECS::Write<TransformComponent>,
    CCL::ECS::Read<CharacterMovementInputComponent>,
    CCL::ECS::Read<PlayerComponent>,
    CCL::ECS::Read<TimeStateComponent>> // ★ 時間コンポーネントを強制要求
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}
    virtual ~PlayerMoveSystem() = default;

    void Update(float dt) override;
};