#pragma once
// プレイヤーのロックオン入力要求を検知し、空間クエリ（距離計算）を用いてターゲットを決定・解除するシステム。

#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/PlayerComponent.h"
#include "Game/Logics/Character/Player/Components/PlayerInputComponent.h"
#include "Engine/Transform/Components/TransformComponent.h"

class PlayerLockOnSystem : public CCL::ECS::IfSystem<PlayerLockOnSystem,
                                                     CCL::ECS::Write<PlayerComponent>,
                                                     CCL::ECS::Read<PlayerInputComponent>,
                                                     CCL::ECS::Read<TransformComponent>>
{
public:
    PlayerLockOnSystem()
        : IfSystem("PlayerLockOnSystem")
    {
    }
    virtual ~PlayerLockOnSystem() = default;

    void Update(float dt) override;
};
