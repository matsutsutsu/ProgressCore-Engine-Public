#include "PlayerMoveSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>

#include "Game/Logics/GameEvent/GameEvents.h"
#include "Engine/Camera/CameraEvents.h" // CameraShakeRequestEvent（着地の画面揺れ要求）
#include "Engine/Core/Math/StringHash.h"


using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

static const uint32_t HASH_TRIG_DOUBLE_JUMP = CCL::Utils::HashString("OnDoubleJump");

// 攻撃時オートフェイシングの回頭速度。
static constexpr float kAttackTurnSpeed = 18.0f;

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      CharacterMovementInputComponent    &inputData,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      AnimParametersComponent            &animParams,
                      const TimeStateComponent           &time) {
        if (m_world->HasComponent<Tag::DeadTag>(id))
            return;
        if (!joltHandle.character)
            return;

        float dt         = time.localDt;
        bool  isGrounded = (joltHandle.character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround);

        // 毎フレーム、ルートモーションのフラグとダッシュの上書きをリセットする
        inputData.isRootMotionActive = false;
        inputData.rootMotionVelocity = Vector3::Zero;

        // =========================================================
        // 1. 接地判定とジャンプ成立の確定
        // =========================================================
        UpdateJumpLogic(dt, isGrounded, inputData, player, animParams, trans.position);

        // =========================================================
        // 2. 旋回とバンク（体の傾き）の反映
        // =========================================================
        UpdateBankingAndRotation(dt, inputData, config.walkSpeed, player, trans, joltHandle.character.GetPtr());
    });
}

// -----------------------------------------------------------------------------------
// [責務] 接地状態の更新と、押した瞬間のジャンプ成立判定
// -----------------------------------------------------------------------------------
void PlayerMoveSystem::UpdateJumpLogic(float dt, bool isGrounded, CharacterMovementInputComponent &inputData, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos)
{
    // 接地を離れてからの経過時間を更新（空中判定のグレースに使う）。
    if (isGrounded)
        player.timeSinceGrounded = 0.0f;
    else
        player.timeSinceGrounded += dt;

    // 直前の落下速度から「着地」の衝撃を計算しイベント発火
    if (isGrounded)
    {
        if (!player.wasGroundedLastFrame)
        {
            if (player.lastFallVelocity < -10.0f)
            {
                float shakePower = std::abs(player.lastFallVelocity) * 0.03f;
                m_world->GetEventBus().Publish(CameraShakeRequestEvent{shakePower, 0.2f});
            }
        }
        player.currentJumpCount = 0;
    }

    player.wasGroundedLastFrame = isGrounded;

    // 空中状態を一元更新（空中攻撃などが参照する唯一の判定元）。
    //   接地を離れて airborneGraceTime を超えて初めて「空中」とみなす。これにより
    //   小さな段差を降りた一瞬で空中攻撃が暴発したり地上攻撃がキャンセルされるのを防ぐ。
    player.isAirborne = (player.timeSinceGrounded > player.airborneGraceTime);

    // 押した瞬間のみジャンプ（エッジ検出）。押しっぱなしでの連続ジャンプを防ぐ。
    bool isPressedThisFrame     = inputData.jumpRequested && !player.wasJumpButtonPressed;
    player.wasJumpButtonPressed = inputData.jumpRequested;

    // ジャンプ成立の判定：接地中の1段目、または空中での2段目まで。
    bool canFirstJump  = isGrounded && (player.currentJumpCount == 0);
    bool canDoubleJump = (player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (isPressedThisFrame && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;
        inputData.jumpRequested = true;

        // 意図的なジャンプは即座に空中扱いへ（グレースは「落下」だけに効かせ、
        // ジャンプ直後の空中攻撃が地上攻撃に化けないようにする）。
        player.timeSinceGrounded = player.airborneGraceTime;
        player.isAirborne        = true;

        // =========================================================
        // 2段ジャンプ専用トリガーの発行
        // 1段目は CharacterAnimSyncSystem が JoltのY速度変化を見て自動で OnJump を出す。
        // 空中での2段目は物理からは判別不可能なため、ここで専用の「宙返り」等を
        // 再生させるためのトリガーを発行する。
        // =========================================================
        if (canDoubleJump)
        {
            animParams.SetTrigger(HASH_TRIG_DOUBLE_JUMP);
        }

        // その場VFX用に「跳んだ」事実を announce する。何段目かを載せ、VFXの出し分けは
        // 購読側(PlayerActionVfxSystem)へ委ねる（現状は2段目だけ土煙を出す）。
        m_world->GetEventBus().Publish(PlayerJumpedEvent{worldPos, player.currentJumpCount});
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
    // =========================================================
    // 攻撃時オートフェイシング（移動より優先）
    //   attackFaceTimer>0 の間は、移動方向ではなく攻撃対象方向(attackFaceYaw)へ
    //   速い回頭で向く。バンク（傾き）は演出上オフにして真っ直ぐ斬らせる。
    // =========================================================
    if (player.attackFaceTimer > 0.0f)
    {
        player.attackFaceTimer -= dt;

        float angleDiff = player.attackFaceYaw - player.currentYaw;
        while (angleDiff > DirectX::XM_PI)
            angleDiff -= DirectX::XM_2PI;
        while (angleDiff < -DirectX::XM_PI)
            angleDiff += DirectX::XM_2PI;

        player.currentYaw += angleDiff * kAttackTurnSpeed * dt;
        player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt); // 攻撃中はバンク解除
    }
    else
    {
        // 移動方向を取得
        Vector3 horizontalMove(inputData.desiredVelocity.x, 0.0f, inputData.desiredVelocity.z);

        if (horizontalMove.LengthSquared() > 0.001f)
        {
            // 移動方向を正規化してプレイヤーが向くべき方向(Yaw)を求める
            horizontalMove.Normalize();
            float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);

            // 現在の向きと目標方向との差分を求める
            float angleDiff = targetYaw - player.currentYaw;
            while (angleDiff > DirectX::XM_PI)
                angleDiff -= DirectX::XM_2PI;
            while (angleDiff < -DirectX::XM_PI)
                angleDiff += DirectX::XM_2PI;

            // 実際に旋回する処理　滑らかに回転するようにしている
            player.currentYaw += angleDiff * player.turnSpeed * dt;

            // プレイヤーの傾き（バンキング）を計算する
            // どれぐらい早く動いているか
            // 現在の速度が最大速度の何％ぐらいなのか求める
            float speedRatio = (walkSpeed > 0.001f) ? (inputData.currentVelocity.Length() / walkSpeed) : 0.0f;
            // どれぐらい方向転換しているか
            float targetRoll = -(angleDiff * 10.0f) * player.bankingMultiplier * speedRatio;

            // 傾き角度の制限
            float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
            targetRoll       = std::clamp(targetRoll, -maxRollRad, maxRollRad);

            // Lerpで滑らかに傾ける
            player.currentRoll = std::lerp(player.currentRoll, targetRoll, 8.0f * dt);
        }
        else
        {
            player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
        }
    }

    Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    trans.rotation = rollRot * yawRot;
    DirectX::XMStoreFloat4(&trans.rotation, DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation)));
    trans.isStatic = false;

    character->SetRotation(JPH::Quat(yawRot.x, yawRot.y, yawRot.z, yawRot.w));
}

REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_PreUpdate);
