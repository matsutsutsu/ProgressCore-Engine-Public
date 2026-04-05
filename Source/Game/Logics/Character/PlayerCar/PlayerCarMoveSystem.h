#pragma once
#include "ECS/System/CCL_System.h"
#include "Engine/GamePlay/Transform/TransformComponent.h"
#include "PlayerCarComponent.h"

class PlayerCarMoveSystem 
    : public CCL::ECS::IfSystem<PlayerCarMoveSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Read<PlayerCarComponent>> {
public:
    PlayerCarMoveSystem();
    virtual ~PlayerCarMoveSystem() = default;

    void Update(float dt) override;
};