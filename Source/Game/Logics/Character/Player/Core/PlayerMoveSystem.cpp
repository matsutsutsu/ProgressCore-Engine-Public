#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

using namespace CCL::ECS;

void PlayerMoveSystem::Update(float dt)
{
    ForEach([&](CharacterMovementComponent &movement,
                PlayerComponent            &player,
                const PlayerDashComponent  &dash,
                const TimeStateComponent   &time) {
        // ダッシュ中は、ダッシュ側が向きと速さを書いている
        if (dash.isDashing) return;

        UpdateGroundLocomotion(player, movement, time.localDt);
    });
}



void PlayerMoveSystem::UpdateGroundLocomotion(PlayerComponent &player, CharacterMovementComponent &movement, float dt)
{
    // --- 走りの解除 ---

    // 走り中に無入力が runIdleReleaseTime 続いたら、歩きに戻す
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

    // --- 速さ ---

    // 走り中は、走る速さを物理へ依頼する
    // 歩きは書かない。書かなければ、物理が Jolt の walkSpeed で歩かせる
    if (player.isRunning) movement.overrideSpeed = player.runSpeed;
}



// PlayerDashSystem の後、PlayerRushSystem の前（どちらも RunAfter で宣言）
REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_Update);
