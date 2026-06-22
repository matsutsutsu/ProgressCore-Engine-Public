#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h" // ★ イベントをinclude

class JustEvadeSystem : public CCL::ECS::IfSystem<JustEvadeSystem,
    CCL::ECS::Write<JustEvadeComponent>,
    CCL::ECS::Read<DashComponent>>
{
public:
    JustEvadeSystem() : IfSystem("JustEvadeSystem") {}

    void Initialize() override;
    void Update(float dt) override;

private:
    // イベント受信時のコールバック関数
    void CheckJustEvade(CCL::ECS::EntityID playerCandidate, CCL::ECS::EntityID otherCandidate);
};