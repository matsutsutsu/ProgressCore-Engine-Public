#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"

class JustEvadeSystem : public CCL::ECS::IfSystem<JustEvadeSystem,
                            CCL::ECS::Write<JustEvadeComponent>,
                            CCL::ECS::Write<DashComponent>,
                            CCL::ECS::Write<CharacterMovementInputComponent>,
                            CCL::ECS::Read<TimeStateComponent>>
{
  public:
    JustEvadeSystem()
        : IfSystem("JustEvadeSystem")
    {
    }

    void Initialize() override;
    void Update(float dt) override;
    void OnGui() override;

  private:
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);
    void BreakChain(CCL::ECS::EntityID playerID); ///< チェインを初期状態へ戻す
};