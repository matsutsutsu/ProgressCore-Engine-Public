#include "PlayerMoveSystem.h"
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

static const uint32_t HASH_TRIG_DOUBLE_JUMP = CCL::Utils::HashString("OnDoubleJump");

// 攻撃時に敵の方へ向き直る速さ（角度差に掛ける係数、1/秒）。
static constexpr float kAttackTurnSpeed = 18.0f;

void PlayerMoveSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      CharacterMovementComponent         &movement,
                      const PlayerInputComponent         &pin,
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

        UpdateJumpLogic(dt, isGrounded, pin.jumpPressed, movement, player, animParams, trans.position);
        UpdateBankingAndRotation(dt, movement, config.walkSpeed, player, trans, joltHandle.character.GetPtr());
    });
}

void PlayerMoveSystem::UpdateJumpLogic(float dt, bool isGrounded, bool jumpPressed, CharacterMovementComponent &movement, PlayerComponent &player, AnimParametersComponent &animParams, const DirectX::XMFLOAT3 &worldPos)
{
    if (isGrounded)
    {
        player.timeSinceGrounded = 0.0f;
        player.currentJumpCount  = 0;
    }
    else
    {
        player.timeSinceGrounded += dt;
    }

    // 接地を離れて airborneGraceTime を超えてから空中とみなす。
    //   小さな段差を降りた一瞬で空中攻撃が出たり、地上攻撃が中断されたりしないようにする。
    player.isAirborne = (player.timeSinceGrounded > player.airborneGraceTime);

    // 跳べるのは、接地中の1段目か、1段目の後の maxJumps 段目まで。
    bool canFirstJump  = isGrounded && (player.currentJumpCount == 0);
    bool canDoubleJump = (player.currentJumpCount > 0 && player.currentJumpCount < player.maxJumps);

    if (jumpPressed && (canFirstJump || canDoubleJump))
    {
        player.currentJumpCount++;

        // 跳ぶことを物理へ渡す。false に戻すのは読む側(JoltCharacterUpdateSystem)なので、
        // 跳ばないときに false を書く必要はない。
        movement.jumpImpulse = true;

        // ジャンプしたらすぐ空中とみなす（猶予は段差から落ちるときだけに使い、
        // ジャンプ直後の空中攻撃が地上攻撃にならないようにする）。
        player.timeSinceGrounded = player.airborneGraceTime;
        player.isAirborne        = true;

        // 1段目の OnJump は、CharacterAnimSyncSystem が接地から離れたときの上向きの速度を見て出す。
        // 空中での2段目は物理の状態からは見分けられないので、ここで専用のトリガーを出す。
        if (canDoubleJump)
        {
            animParams.SetTrigger(HASH_TRIG_DOUBLE_JUMP);
        }

        // 跳んだことを何段目かと一緒にイベントで知らせる。VFX の出し分けは
        // 購読側(PlayerActionVfxSystem)が決める（今は2段目だけ土煙を出す）。
        m_world->GetEventBus().Publish(PlayerJumpedEvent{worldPos, player.currentJumpCount});
    }
}

void PlayerMoveSystem::UpdateBankingAndRotation(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player, TransformComponent &trans, JPH::CharacterVirtual *character)
{
    // attackFaceTimer>0 の間は、移動方向より優先して attackFaceYaw へ速く向き直る。
    //   傾きは 0 に戻し、まっすぐ斬るように見せる。
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
        Vector3 horizontalMove(movement.desiredVelocity.x, 0.0f, movement.desiredVelocity.z);

        if (horizontalMove.LengthSquared() > 0.001f)
        {
            horizontalMove.Normalize();
            float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);

            // 差を -π〜π に収める
            float angleDiff = targetYaw - player.currentYaw;
            while (angleDiff > DirectX::XM_PI)
                angleDiff -= DirectX::XM_2PI;
            while (angleDiff < -DirectX::XM_PI)
                angleDiff += DirectX::XM_2PI;

            player.currentYaw += angleDiff * player.turnSpeed * dt;

            // 傾きは「曲がる角度 × 速さ」に比例させる。速さは walkSpeed に対する比で測る。
            float speedRatio = (walkSpeed > 0.001f) ? (movement.currentVelocity.Length() / walkSpeed) : 0.0f;
            float targetRoll = -(angleDiff * 10.0f) * player.bankingMultiplier * speedRatio;

            float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
            targetRoll       = std::clamp(targetRoll, -maxRollRad, maxRollRad);

            player.currentRoll = std::lerp(player.currentRoll, targetRoll, 8.0f * dt);
        }
        else
        {
            player.currentRoll = std::lerp(player.currentRoll, 0.0f, 10.0f * dt);
        }
    }

    Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    // 見た目(Transform)には傾きを含め、物理(Jolt)には yaw だけを渡す。
    trans.rotation = rollRot * yawRot;
    DirectX::XMStoreFloat4(&trans.rotation, DirectX::XMVector4Normalize(DirectX::XMLoadFloat4(&trans.rotation)));
    trans.isStatic = false;

    character->SetRotation(JPH::Quat(yawRot.x, yawRot.y, yawRot.z, yawRot.w));
}

// ジャンプの要求(jumpImpulse)を、物理(JoltCharacterUpdateSystem)より前に書く。
REGISTER_LOGIC_SYSTEM(PlayerMoveSystem, Priority::LogicStage::L02_PreUpdate);
