#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void PlayerMoveSystem::Update(float /*dt*/)
{
    ForEach([&](CharacterMovementComponent &movement,
                PlayerComponent            &player,
                const PlayerDashComponent  &dash,
                const TimeStateComponent   &time) {
        // ダッシュ中はダッシュ側が方向と速さを書いている
        if (dash.isDashing)
        {
            return;
        }

        UpdateGroundLocomotion(player, movement, time.localDt);
    });
}

void PlayerMoveSystem::UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt)
{
    // 走行中、無入力が runIdleReleaseTime 続いたら歩きに戻す。
    if (player.isRunning)
    {
        const bool hasMoveInput = movement.desiredVelocity.LengthSquared() > 0.001f;
        if (hasMoveInput)
        {
            player.runIdleTimer = 0.0f;
        }
        else
        {
            player.runIdleTimer += dt;
            if (player.runIdleTimer >= player.runIdleReleaseTime)
            {
                player.isRunning = false;
            }
        }
    }

    if (player.isRunning)
    {
        movement.overrideSpeed = player.runSpeed;
        player.locomotion      = PlayerComponent::GroundLocomotion::Run;
    }
    else
    {
        // 歩きのときは overrideSpeed を書かない。読まれた時点で既定へ戻るので、書かなければ config.walkSpeed で歩く。
        player.locomotion = PlayerComponent::GroundLocomotion::Walk;
    }
}

// PlayerDashSystem の後（GetRunAfter）、PlayerRushSystem の前に、物理(L03以降)より先に速さを書く。
REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_Update);
