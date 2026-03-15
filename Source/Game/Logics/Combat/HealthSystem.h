#pragma once
#include "ECS/System/CCL_System.h"

#include "Game/Logics/Combat/HealthComponent.h"

// 【解決】HPの増減と死亡判定（Tagの付与）のみを行うシステム
class HealthSystem : public CCL::ECS::IfSystem<HealthSystem,
                         CCL::ECS::Write<HealthComponent>> {
  public:
    HealthSystem() : IfSystem("HealthSystem") {}
    void Update(float dt) override;
};