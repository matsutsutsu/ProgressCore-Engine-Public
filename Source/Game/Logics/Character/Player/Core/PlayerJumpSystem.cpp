#include "PlayerJumpSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"

#include "Game/Core/GameEvents.h"
#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    // ジャンプで引くアニメのトリガー（1段目 / 2段目）
    const uint32_t HASH_TRIG_JUMP        = CCL::Utils::HashString("Jump");
    const uint32_t HASH_TRIG_DOUBLE_JUMP = CCL::Utils::HashString("DoubleJump");
}



void PlayerJumpSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      CharacterMovementComponent         &movement,
                      const PlayerInputComponent         &pin,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams,
                      const PlayerRushComponent          &rush,
                      const TimeStateComponent           &time) {
        if (m_world->HasComponent<Tag::DeadTag>(id)) return;

        // ラッシュの回避離脱・待機は操作不能。ジャンプを受け付けない
        if (rush.isActive && (rush.phase == RushPhase::Locked || rush.phase == RushPhase::Ready))
        {
            return;
        }

        const float dt         = time.localDt;
        const bool  isGrounded = (joltHandle.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // ジャンプするかどうかのフラグ
        // ラッシュ中はジャンプできないようにする
        const bool jumpPressed = pin.jumpPressed && !rush.isActive;

        // 物理的なジャンプ処理を行う関数
        UpdateJumpLogic(dt, isGrounded, jumpPressed, movement, player, animParams, GetCharacterWorldPosition(joltHandle));
    });
}



void PlayerJumpSystem::UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos)
{
    // --- 接地 ---

    if (isGrounded)
    {
        player.timeSinceGrounded = 0.0f;
        player.currentJumpCount  = 0;
    }
    // 接地していない場合は地面から離れた時間を計測する
    else
    {
        player.timeSinceGrounded += dt;
    }

    // 接地を離れて airborneGraceTime を過ぎてから、空中にいると判定する
    // 段差を降りた一瞬で、地上攻撃のアニメーションなどが途切れないようにするため
    player.isAirborne = (player.timeSinceGrounded > player.airborneGraceTime);

    // --- ジャンプ ---

    // 跳べるのは、接地中の1段目と、空中で maxJumps 段目まで
    // 段差から落ちた直後は跳べない（猶予はこの判定に使わない）
    const bool canFirstJump  = isGrounded && (player.currentJumpCount == 0);
    const bool canDoubleJump =(player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (jumpPressed && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;

        // 跳ぶことを物理処理へ依頼する
        // 物理処理側が読んだら false に戻すので、跳ばないときは書かなくていい
        movement.jumpImpulse = true;

        // 跳んだらすぐ空中判定にする
        // 跳んだ直後の攻撃を、地上攻撃でなく空中攻撃にするため
        player.timeSinceGrounded = player.airborneGraceTime;
        player.isAirborne        = true;

        // アニメのトリガーを引く。1段目は Jump、2段目は DoubleJump
        if (canDoubleJump)
        {
            animParams.SetTrigger(HASH_TRIG_DOUBLE_JUMP);
        }
        else
        {
            animParams.SetTrigger(HASH_TRIG_JUMP);
        }

        // ジャンプした際のパーティクル発生イベントを送信する
        m_world->GetEventBus().Publish(PlayerJumpedEvent{worldPos, player.currentJumpCount});
    }
}



// ジャンプの依頼（jumpImpulse）を、物理より前に書く
REGISTER_LOGIC_SYSTEM(PlayerJumpSystem, Priority::LogicStage::L02_PreUpdate);
