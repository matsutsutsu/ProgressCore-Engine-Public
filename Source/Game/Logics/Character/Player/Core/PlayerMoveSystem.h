#pragma once
// ===========================================================================
// 概要: プレイヤーの地上の歩き／走りを処理する System。
//
// 責務:
//   - 走行中、無入力が runIdleReleaseTime 続いたら歩きに戻す
//   - 走行中は走りの速さを移動の上書きへ書き、歩きか走りかを locomotion へ書く
//
// 備考: 走行に入るきっかけ（ダッシュの終了で isRunning を立てる）は PlayerDashSystem。
//       ダッシュ中はダッシュ側が速さを書くので、ここは何もしない。
//       ダッシュが終わったステップの速さもここで書くため、PlayerDashSystem の後に走る
//       （上書き要求は1ステップ限りなので、前に走るとそのステップだけ歩き速度に落ちる）。
//       ジャンプと向きは PlayerJumpFacingSystem。
// ===========================================================================
#include "Engine/ECS/System/CCL_IfSystem.h"
#include "Engine/Time/Components/TimeState.h"
#include "Engine/Physics/Components/Character/CharacterMovementComponent.h"
#include "Game/Logics/Character/Player/Core/PlayerComponent.h"
#include "Game/Logics/Character/Player/Dash/PlayerDashComponent.h"

#include <vector>

class PlayerMoveSystem : public CCL::ECS::IfSystem<PlayerMoveSystem,
                                                   CCL::ECS::Write<CharacterMovementComponent>,
                                                   CCL::ECS::Write<PlayerComponent>,
                                                   CCL::ECS::Read<PlayerDashComponent>,
                                                   CCL::ECS::Read<TimeStateComponent>>
{
public:
    PlayerMoveSystem() : IfSystem("PlayerMoveSystem") {}

    void Update(float dt) override;

    // ダッシュの終了（isRunning）を同じステップで受け取り、速さを書くために後に走る。
    std::vector<const char *> GetRunAfter() const override { return {"PlayerDashSystem"}; }

private:
    // 歩き／走り。走行中は、無入力が runIdleReleaseTime 続いたら歩きに戻す。
    void UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt);
};
