#pragma once
// ===========================================================================
// 概要: プレイヤーの地上の歩きと走りを処理する System
//
// 責務:
//   - 走り中に無入力が runIdleReleaseTime 続いたら、歩きに戻す
//   - 走り中は、走る速さを overrideSpeed へ書く
//
// 備考:
//   走りに入るきっかけ（ダッシュの終わりで isRunning を立てる）は PlayerDashSystem
//   ダッシュ中はダッシュ側が速さを書くので、ここは何もしない
//   ジャンプは PlayerJumpSystem、向きは PlayerFacingSystem
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"

#include <vector>

class PlayerMoveSystem : public CCL::ECS::IfSystem<
    PlayerMoveSystem,
    CCL::ECS::Write<CharacterMovementComponent>,
    CCL::ECS::Write<PlayerComponent>,
    CCL::ECS::Read<PlayerDashComponent>,
    CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}

    void Update(float dt) override;

    // ダッシュが終わったステップの速さも、ここで書く
    // overrideSpeed は1ステップ限りなので、前に走るとそのステップだけ歩きの速さに落ちる
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem"}; }

private:
    // 走りを解くかを決め、歩きか走りかに合わせて速さを書く
    void UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt);
};
