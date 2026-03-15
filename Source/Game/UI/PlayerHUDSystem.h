// PlayerHUDSystem.h
#pragma once
#include "ECS/System/CCL_System.h"
#include "Game/Logics/Character/Player/PlayerComponent.h"
#include "Game/Logics/Combat/HealthComponent.h"

// 引数なしのすっきりしたコンストラクタになる
class PlayerHUDSystem : public CCL::ECS::IfSystem<PlayerHUDSystem,
                            CCL::ECS::Read<PlayerComponent>,
                            CCL::ECS::Read<HealthComponent>> {
  public:
    PlayerHUDSystem() : IfSystem("PlayerHUDSystem") {} 
    void Update(float dt) override;
};