#include "PlayerFacingSystem.h"
#include "Engine/ECS/Core/CCL_World.h"
#include "Game/Logics/Combat/Components/DeadTag.h"
#include "Engine/ECS/System/CCL_SystemRegistry.h"
#include "Engine/Core/Systems/SystemPriority.h"
#include "Engine/Core/Math/Angle.h"
#include <cmath>
#include <algorithm>
#include <DirectXMath.h>

using namespace CCL::ECS;
using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kAttackTurnSpeed = 18.0f;  // 攻撃時に敵へ向き直る速さ（角度差に掛ける係数、1/秒）
    constexpr float kRollPerAngle    = 10.0f;  // 角度差 1rad あたりの傾き。bankingMultiplier と速さの比も掛ける
    constexpr float kRollFollowSpeed = 8.0f;   // 曲がっているとき、目標の傾きへ寄せる速さ（1/秒）
    constexpr float kRollReturnSpeed = 10.0f;  // 攻撃中・止まっているとき、傾きを 0 へ戻す速さ（1/秒）
    constexpr float kMinMoveInputSq  = 0.001f; // 移動の入力があると判定する長さの2乗
}



void PlayerFacingSystem::Update(float rawDt)
{
    ForEachWithID([&](EntityID                            id,
                      TransformComponent                 &trans,
                      const CharacterMovementComponent   &movement,
                      const JoltCharacterConfigComponent &config,
                      const JoltCharacterHandleComponent &joltHandle,
                      PlayerComponent                    &player,
                      const PlayerRushComponent          &rush,
                      const TimeStateComponent           &time) {
        if (m_world->HasComponent<Tag::DeadTag>(id)) return;

        const float dt = time.localDt;

        // ラッシュの回避離脱・待機は操作不能。移動の向きへは回さない
        // 向き直り（HoldEvadeFacing が書く相手の方）だけを許し、相手を正面に置いて回避のアニメと向きを合わせる
        // 傾きも 0 へ戻す。戻さないと、ダッシュで曲がったときの傾きが残り、傾いたまま待機してしまう
        // 間合い詰め・連撃は、ふつうの処理で向き直り（attackFaceYaw）を許す
        if (rush.isActive && (rush.phase == RushPhase::Locked || rush.phase == RushPhase::Ready))
        {
            if (player.attackFaceTimer > 0.0f) TurnToAttackFace(dt, player);
            else player.currentRoll = std::lerp(player.currentRoll, 0.0f, kRollReturnSpeed * dt);

            ApplyRotation(player, trans, joltHandle);
            return;
        }

        // 向きと傾きの更新
        UpdateYawAndRoll(dt, movement, config.walkSpeed, player);

        // 傾きを含む向きを見た目に、yaw だけを物理に書く
        ApplyRotation(player, trans, joltHandle);
    });
}



// 向き（yaw）と傾き（roll）を1ステップ進める
// 攻撃中は敵の方、それ以外は移動の向きへ回す
void PlayerFacingSystem::UpdateYawAndRoll(float dt, const CharacterMovementComponent &movement, float walkSpeed, PlayerComponent &player)
{
    // 処理フロー:
    // 1. 攻撃中なら、敵の方へ速く向き直る
    // 2. 移動の入力がなければ、傾きだけ戻す
    // 3. 移動中なら、移動の向きへ回し、曲がる角度に合わせて傾ける

    // --- 攻撃中 ---

    // 移動の向きより優先して、敵へ速く向き直る
    if (player.attackFaceTimer > 0.0f)
    {
        TurnToAttackFace(dt, player);
        return;
    }

    // --- 止まっている ---

    // 向きはそのままにして、傾きだけ 0 へ戻す
    Vector3 horizontalMove = {movement.desiredVelocity.x, 0.0f, movement.desiredVelocity.z};
    if (horizontalMove.LengthSquared() <= kMinMoveInputSq)
    {
        player.currentRoll = std::lerp(player.currentRoll, 0.0f, kRollReturnSpeed * dt);
        return;
    }

    // --- 移動中 ---

    // 移動の向きへ、近い回り方で向き直る
    horizontalMove.Normalize();
    const float targetYaw = atan2f(horizontalMove.x, horizontalMove.z);
    const float angleDiff = CCL::Math::WrapRadians(targetYaw - player.currentYaw);
    player.currentYaw += angleDiff * player.turnSpeed * dt;

    // 傾きは、回す前の角度差から求める
    // 向き直るにつれて角度差が減り、傾きも戻っていく
    const float targetRoll = CalcBankingRoll(angleDiff, movement, walkSpeed, player);

    // 目標のroll角度に滑らかに近づける
    player.currentRoll     = std::lerp(player.currentRoll, targetRoll, kRollFollowSpeed * dt);
}



void PlayerFacingSystem::TurnToAttackFace(float dt, PlayerComponent &player)
{
    player.attackFaceTimer -= dt;

    // 角度差に比例して回すので、離れているほど速く回り、近づくほどゆっくり止まる
    const float angleDiff = CCL::Math::WrapRadians(player.attackFaceYaw - player.currentYaw);
    player.currentYaw += angleDiff * kAttackTurnSpeed * dt;

    // 傾きは 0 に戻し、まっすぐ斬る・跳ぶように見せる
    player.currentRoll = std::lerp(player.currentRoll, 0.0f, kRollReturnSpeed * dt);
}



// 旋回時の傾きの目標（rad）を返す
// 曲がる角度と速さに比例させ、maxBankingAngle で止める
float PlayerFacingSystem::CalcBankingRoll(float angleDiff, const CharacterMovementComponent &movement, float walkSpeed, const PlayerComponent &player) const
{
    // 速さは walkSpeed に対する比で測る
    float speedRatio = 0.0f;
    if (walkSpeed > 0.001f) speedRatio = movement.currentVelocity.Length() / walkSpeed;

    // 右へ曲がる（angleDiff が正）ときは roll を負にして、曲がる側へ体を倒す
    const float targetRoll = -(angleDiff * kRollPerAngle) * player.bankingMultiplier * speedRatio;

    const float maxRollRad = DirectX::XMConvertToRadians(player.maxBankingAngle);
    return std::clamp(targetRoll, -maxRollRad, maxRollRad);
}



// 傾きを含む向きを Transform に、yaw だけを Jolt に書く
void PlayerFacingSystem::ApplyRotation(const PlayerComponent &player, TransformComponent &trans, const JoltCharacterHandleComponent &joltHandle)
{
    const Quaternion yawRot  = Quaternion::CreateFromAxisAngle(Vector3::UnitY, player.currentYaw);
    const Quaternion rollRot = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, player.currentRoll);

    // roll を先に掛けてから yaw で回す（SimpleMath の * は左の回転が先）
    // 逆にすると、横を向いたときに前のめりに傾く
    Quaternion visualRot = rollRot * yawRot;
    visualRot.Normalize();

    // 見た目には傾きを含め、物理には yaw だけを渡す
    // カプセルを傾けると、地面や壁との当たり方が変わる
    trans.isStatic = false;
    SetCharacterRotation(trans, joltHandle, visualRot, yawRot);
}



// 向きを、物理より前に書く
REGISTER_LOGIC_SYSTEM(PlayerFacingSystem, Priority::LogicStage::L02_PreUpdate);
