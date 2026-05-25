#pragma once
#include "Engine/ECS/System/CCL_System.h"
#include "Engine/Transform/Components/TransformComponent.h"
#include "PlayerCarComponent.h"

class PlayerCarMoveSystem 
    : public CCL::ECS::IfSystem<PlayerCarMoveSystem,
                                CCL::ECS::Write<TransformComponent>,
                                CCL::ECS::Write<PlayerCarComponent>> { // ★ ReadをWriteに変更！
public:
    PlayerCarMoveSystem();
    virtual ~PlayerCarMoveSystem() = default;

    void Update(float dt) override;
};