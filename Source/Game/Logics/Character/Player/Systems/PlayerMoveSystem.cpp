#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Core/Math/StringHash.h" // ★追加: ハッシュ計算用

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

static const uint32_t HASH_TRIG_DOUBLE_JUMP = CCL::Utils::HashString("OnDoubleJump");

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      CharacterMovementInputComponent    &inputData,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams, // ★追加
                      const TimeStateComponent           &time) {
        if (_world->HasComponent<Tag::DeadTag>(id))
            return;
        if (!joltHandle.character)
            return;

        float dt         = time.localDt;
        bool  isGrounded = (joltHandle.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // =========================================================
        // 1. 操作ハック（時間の魔法）
        // =========================================================
        UpdateJumpLogic(dt, isGrounded, inputData, player, animParams); // ★引数追加

        // =========================================================
        // 2. 視覚ハック（空間の魔法）
        // =========================================================
        UpdateBankingAndRotation(dt, inputData, config.walkSpeed, player, trans, joltHandle.character.GetPtr());
    });
}

// -----------------------------------------------------------------------------------
// [責務] 操作の遊び（バッファ）を作り、人間の反射神経のズレを吸収する
// -----------------------------------------------------------------------------------
void PlayerMoveSystem::UpdateJumpLogic(float dt, bool isGrounded, CharacterMovementInputComponent &inputData, PlayerComponent &player, AnimParametersComponent &animParams)
{
    // 直前の落下速度から「着地」の衝撃を計算しイベント発火
    if (isGrounded)
    {
        if (!player.wasGroundedLastFrame)
        {
            if (player.lastFallVelocity < -10.0f)
            {
                float shakePower = std::abs(player.lastFallVelocity) * 0.03f;
                _world->GetEventBus().Publish(CameraShakeRequestEvent{shakePower, 0.2f});
            }
        }
        player.timeSinceGrounded = 0.0f;
        player.currentJumpCount  = 0;
    }
    else
    {
        player.timeSinceGrounded += dt;
    }

    player.wasGroundedLastFrame = isGrounded;

    // エッジ検出とバッファタイマーの更新
    bool isPressedThisFrame     = inputData.jumpRequested && !player.wasJumpButtonPressed;
    player.wasJumpButtonPressed = inputData.jumpRequested;

    if (isPressedThisFrame)
    {
        player.jumpBufferTimer = player.jumpBufferWindow;
    }
    else if (player.jumpBufferTimer > 0.0f)
    {
        player.jumpBufferTimer -= dt;
    }

    // ジャンプ成立の判定
    bool canFirstJump  = (player.timeSinceGrounded <= player.coyoteTimeWindow) && (player.currentJumpCount == 0);
    bool canDoubleJump = (player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (player.jumpBufferTimer > 0.0f && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;
        inputData.jumpRequested = true;

        // =========================================================
        // ★ AAAアニメーションハック: 固有アクションの伝達
        // 1段目は CharacterAnimSyncSystem が JoltのY速度変化を見て自動で OnJump を出す。
        // 空中での2段目は物理からは判別不可能なため、ここで専用の「宙返り」等を
        // 再生させるためのトリガーを発行する。
        // =========================================================
        if (canDoubleJump)
        {
            animParams.SetTrigger(HASH_TRIG_DOUBLE_JUMP);
        }

        player.jumpBufferTimer   = 0.0f;
        player.timeSinceGrounded = 999.0f;
    }
    else
    {
        inputData.jumpRequested = false;
    }
}

// -----------------------------------------------------------------------------------
// [責務] アニメーションを使わずに旋回の勢いを「体の傾き」として視覚化し、物理エンジンには純粋な方向のみを渡す
// -----------------------------------------------------------------------------------
void PlayerMoveSystem::UpdateBankingAndRotation(float dt, const CharacterMovementInputComponent &inputData, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character)
{
    Vector3 horizontalMove(inputData.desiredVelocity.x, 0.0f, inputData.desiredVelocity.z);

    if (horizontalMove.LengthSquared() > 0.001f)
    {
        horizontalMove.Normalize();
        float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);

        float angleDiff = targetYaw - player.currentYaw;
        while (angleDiff > DirectX::XM_PI)
            angleDiff -= DirectX::XM_2PI;
        while (angleDiff < -DirectX::XM_PI)
            angleDiff += DirectX::XM_2PI;

        player.currentYaw += angleDiff * player.turnSpeed * dt;

        float speedRatio = (walkSpeed > 0.001f) ? (inputData.currentVelocity.Length() / walkSpeed) : 0.0f;
        float targetRoll = -(angleDiff * 10.0f) * player.bankingMultiplier * speedRatio;

        float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
        targetRoll       = std::clamp(targetRoll, -maxRollRad, maxRollRad);

        player.currentRoll = std::lerp(player.currentRoll, targetRoll, 8.0f * dt);
    }
    else
    {
        player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
    }

    Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    trans.rotation = rollRot * yawRot;
    DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation));
    trans.isStatic = false;

    character->SetRotation(JPH::Quat(yawRot.x, yawRot.y, yawRot.z, yawRot.w));
}

REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_PreUpdate);