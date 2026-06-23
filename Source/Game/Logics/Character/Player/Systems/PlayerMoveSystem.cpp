#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h> // XM_PIの使用に必要

#include "Game/Logics/GameEvent/GameEvents.h"

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      CharacterMovementInputComponent    &inputData,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      const TimeStateComponent           &time) {
        if (_world->HasComponent<Tag::DeadTag>(id))
            return;
        if (!joltHandle.character)
            return; // Jolt実体が無い場合はスキップ

        float dt         = time.localDt;
        bool  isGrounded = (joltHandle.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // =========================================================
        // 1. 操作ハック（時間の魔法）
        // =========================================================
        UpdateJumpLogic(dt, isGrounded, inputData, player);

        // =========================================================
        // 2. 視覚ハック（空間の魔法）
        // =========================================================
        UpdateBankingAndRotation(dt, inputData, config.walkSpeed, player, trans, joltHandle.character.GetPtr());
    });
}

// -----------------------------------------------------------------------------------
// [責務] 操作の遊び（バッファ）を作り、人間の反射神経のズレを吸収する
// -----------------------------------------------------------------------------------
void PlayerMoveSystem::UpdateJumpLogic(float dt, bool isGrounded, CharacterMovementInputComponent &inputData, PlayerComponent &player)
{
    // 直前の落下速度から「着地」の衝撃を計算しイベント発火
    if (isGrounded)
    {
        if (!player.wasGroundedLastFrame)
        {
            // 落下速度が一定以上(例：-10.0f)ならカメラシェイクを要求
            if (player.lastFallVelocity < -10.0f)
            {
                // 落下が速いほど揺れを強くする
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

    // 状態の保存
    player.wasGroundedLastFrame = isGrounded;

    // コヨーテタイマーの更新
    if (isGrounded)
    {
        player.timeSinceGrounded = 0.0f;
        player.currentJumpCount  = 0;
    }
    else
    {
        player.timeSinceGrounded += dt; // 空中なら時間を計測
    }

    // エッジ検出とバッファタイマーの更新
    bool isPressedThisFrame     = inputData.jumpRequested && !player.wasJumpButtonPressed;
    player.wasJumpButtonPressed = inputData.jumpRequested;

    if (isPressedThisFrame)
    {
        player.jumpBufferTimer = player.jumpBufferWindow; // ボタンを押したら「予約」
    }
    else if (player.jumpBufferTimer > 0.0f)
    {
        player.jumpBufferTimer -= dt; // 予約の消費
    }

    // ジャンプ成立の判定
    bool canFirstJump  = (player.timeSinceGrounded <= player.coyoteTimeWindow) && (player.currentJumpCount == 0);
    bool canDoubleJump = (player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (player.jumpBufferTimer > 0.0f && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;
        inputData.jumpRequested = true;

        player.jumpBufferTimer   = 0.0f;   // 成立したら予約を破棄
        player.timeSinceGrounded = 999.0f; // 連続コヨーテを防ぐために莫大な時間を代入
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

        // 最短回転角度の差分計算 (-π 〜 π)
        float angleDiff = targetYaw - player.currentYaw;
        while (angleDiff > DirectX::XM_PI)
            angleDiff -= DirectX::XM_2PI;
        while (angleDiff < -DirectX::XM_PI)
            angleDiff += DirectX::XM_2PI;

        // ロジック用のYaw(左右の向き)を安全に補間
        player.currentYaw += angleDiff * player.turnSpeed * dt;

        // --- バンキング(傾き)の計算 ---
        // 旋回量(angleDiff)と現在の移動速度から傾きの目標値を算出
        float speedRatio = (walkSpeed > 0.001f) ? (inputData.currentVelocity.Length() / walkSpeed) : 0.0f;

        // 急旋回・高速であるほど深く傾く (-angleDiff で内側に倒し込む)
        float targetRoll = -(angleDiff * 10.0f) * player.bankingMultiplier * speedRatio;

        // 限界角(度)をラジアンに変換してクランプ
        float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
        targetRoll       = std::clamp(targetRoll, -maxRollRad, maxRollRad);

        // Roll(傾き)をバネのように滑らかに補間 (C++20のstd::lerpを使用)
        player.currentRoll = std::lerp(player.currentRoll, targetRoll, 8.0f * dt);
    }
    else
    {
        // 停止時は直立姿勢へ戻る
        player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
    }

    // --- 物理と描画の完全分離 (DODの極意) ---
    Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    // 最終的な「描画用Transform」にはRollの傾きを含める
    trans.rotation = rollRot * yawRot;
    DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation));
    trans.isStatic = false;

    // 「物理エンジン(Jolt)」には、傾きを一切含まない純粋なYawだけを渡す
    // ※これを行わないと物理カプセルが傾いて地面に突き刺さります
    character->SetRotation(JPH::Quat(yawRot.x, yawRot.y, yawRot.z, yawRot.w));
}

REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_Update);