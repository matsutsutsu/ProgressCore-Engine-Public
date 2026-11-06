#include "PlayerJumpFacingSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>

#include "Game/Core/GameEvents.h"
#include "Engine/Core/Math/StringHash.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

// 2段目のジャンプで引くアニメのトリガー
static const uint32_t HASH_TRIG_DOUBLE_JUMP = CCL::Utils::HashString("OnDoubleJump");

// 攻撃時に敵へ向き直る速さ（角度差に掛ける係数、1/秒）
static constexpr float kAttackTurnSpeed = 18.0f;



void PlayerJumpFacingSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      CharacterMovementComponent         &movement,
                      const PlayerInputComponent         &pin,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams,
                      const PlayerRushComponent          &rush,
                      const TimeStateComponent           &time) {
        if (m_world->HasComponent<Tag::DeadTag>(id)) return;
        
        // ラッシュの回避離脱・待機は操作不能。向きもジャンプも受け付けない
        // 間合い詰め・連撃は、向き直り（attackFaceYaw）を許す
        if (rush.isActive && (rush.phase == RushPhase::Locked || rush.phase == RushPhase::Ready))
        {
            return;
        }

        float dt         = time.localDt;
        bool  isGrounded = (joltHandle.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // ラッシュ中はジャンプさせない。連撃はその場で斬り続けるもので、跳ぶと空中のコンボに変わってしまう
        const bool jumpPressed = pin.jumpPressed && !rush.isActive;

        UpdateJumpLogic(dt, isGrounded, jumpPressed, movement, player, animParams, GetCharacterWorldPosition(joltHandle));
        UpdateBankingAndRotation(dt, movement, config.walkSpeed, player, trans, joltHandle);
    });
}



void PlayerJumpFacingSystem::UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos)
{
    // --- 接地 ---

    if (isGrounded)
    {
        player.timeSinceGrounded = 0.0f;
        player.currentJumpCount  = 0;
    }
    else
    {
        player.timeSinceGrounded += dt;
    }

    // 接地を離れて airborneGraceTime を過ぎてから、空中とみなす
    // 段差を降りた一瞬で、空中攻撃が出たり地上攻撃が途切れたりしないようにする
    player.isAirborne = (player.timeSinceGrounded > player.airborneGraceTime);

    // --- ジャンプ ---

    // 跳べるのは、接地中の1段目と、空中で maxJumps 段目まで
    // 段差から落ちた直後は跳べない（猶予はこの判定に使わない）
    bool canFirstJump  = isGrounded && (player.currentJumpCount == 0);
    bool canDoubleJump = (player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (jumpPressed && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;

        // 跳ぶことを物理へ依頼する
        // 物理が読んだら false に戻すので、跳ばないときは書かなくてよい
        movement.jumpImpulse = true;

        // 跳んだらすぐ空中とみなす
        // 跳んだ直後の攻撃を、地上攻撃でなく空中攻撃にするため
        player.timeSinceGrounded = player.airborneGraceTime;
        player.isAirborne        = true;

        // 2段目だけ、ここでアニメのトリガーを引く
        // 1段目は CharacterAnimSyncSystem が離陸の速度から OnJump を引く
        // 空中の2段目は、物理の状態からは見分けられない
        if (canDoubleJump) animParams.SetTrigger(HASH_TRIG_DOUBLE_JUMP);

        // 跳んだことを、何段目かと一緒に知らせる
        // どの VFX を出すかは購読側（PlayerActionVfxSystem）が決める
        m_world->GetEventBus().Publish(PlayerJumpedEvent{worldPos, player.currentJumpCount});
    }
}



void PlayerJumpFacingSystem::UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, const JoltCharacterHandleComponent &joltHandle)
{
    // --- 向きと傾き ---

    if (player.attackFaceTimer > 0.0f)
    {
        // 攻撃中は、移動の向きより優先して敵へ速く向き直る
        // 傾きは 0 に戻し、まっすぐ斬るように見せる
        player.attackFaceTimer -= dt;

        float angleDiff = player.attackFaceYaw - player.currentYaw;
        while (angleDiff > DirectX::XM_PI)
            angleDiff -= DirectX::XM_2PI;
        while (angleDiff < -DirectX::XM_PI)
            angleDiff += DirectX::XM_2PI;

        player.currentYaw += angleDiff * kAttackTurnSpeed * dt;
        player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
    }
    else
    {
        Vector3 horizontalMove(movement.desiredVelocity.x, 0.0f, movement.desiredVelocity.z);

        if (horizontalMove.LengthSquared() > 0.001f)
        {
            horizontalMove.Normalize();
            float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);

            // 差を -π〜π に収め、近い回り方で向き直る
            float angleDiff = targetYaw - player.currentYaw;
            while (angleDiff > DirectX::XM_PI)
                angleDiff -= DirectX::XM_2PI;
            while (angleDiff < -DirectX::XM_PI)
                angleDiff += DirectX::XM_2PI;

            player.currentYaw += angleDiff * player.turnSpeed * dt;

            // 傾きは「曲がる角度 × 速さ」に比例させる
            // 速さは walkSpeed に対する比で測る
            float speedRatio = (walkSpeed > 0.001f) ? (movement.currentVelocity.Length() / walkSpeed) : 0.0f;
            float targetRoll = -(angleDiff * 10.0f) * player.bankingMultiplier * speedRatio;

            float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
            targetRoll       = std::clamp(targetRoll, -maxRollRad, maxRollRad);

            player.currentRoll = std::lerp(player.currentRoll, targetRoll, 8.0f * dt);
        }
        else
        {
            // 止まっているときは、傾きを 0 へ戻す
            player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
        }
    }

    // --- 書き込み ---

    Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    Quaternion visualRot = rollRot * yawRot;
    visualRot.Normalize();

    // 見た目には傾きを含め、物理には yaw だけを渡す
    trans.isStatic = false;
    SetCharacterRotation(trans, joltHandle, visualRot, yawRot);
}



// ジャンプの依頼（jumpImpulse）と向きを、物理より前に書く
REGISTER_LOGIC_SYSTEM(PlayerJumpFacingSystem, Priority::LogicStage::L02_PreUpdate);
