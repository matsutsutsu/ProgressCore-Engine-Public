#pragma once
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Game/Logics/Character/Player/Components/JustEvadeComponent.h"
#include "Game/Logics/Character/Player/Components/DashComponent.h"
#include "Engine/Time/TimeState.h"
#include "Engine/Physics/Components/Collision/JoltCollisionEvent.h"
#include "Engine/Physics/Components/Character/CharacterMovementInputComponent.h"

struct DroneComponent; // ポインタ参照のみのため前方宣言で足りる

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

    /// そのドローンが「今ぶつかったらジャスト回避が成立する相手」かを判定する門番。
    /// 陣形を組んでいるだけのドローンに突っ込んでも回避が暴発しないようにする。
    bool IsDroneHostile(const DroneComponent *drone) const;
};