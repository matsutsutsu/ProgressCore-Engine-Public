#pragma once
// ===========================================================================
// 概要: 空中攻撃から出す急降下の、構えと落下の速度を与える System
//
// 責務:
//   - 構え（AirDiveStart）の間は、空中に止める
//   - 落下（AirDiveLoop）の間は、斜め前の下へ速く落とす
//
// 備考: 出す・着地で叩きつけるといった流れはアニメグラフが決める。ここはステートを見て速度を書くだけ
//       今のステップの遷移を見るので、AnimControllerSystem より後、物理より前に走る
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"

#include <vector>

class PlayerAirDiveSystem : public CCL::ECS::IfSystem<
    PlayerAirDiveSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Read<AnimStateMachineComponent>,
    CCL::ECS::Read<PlayerComponent>>
{
public:
    PlayerAirDiveSystem() : IfSystem("PlayerAirDiveSystem") {}

    // ルートモーションの速度より優先して上書きするので、RootMotionSystem の後に走る
    std::vector<const char *> GetRunAfter() const override { return {"AnimControllerSystem", "RootMotionSystem"}; }

    void Update(float dt) override;
};
