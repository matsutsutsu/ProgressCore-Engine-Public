#include "BossActionCommon.h"
#include "BossActionGeometry.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace BossActionGeometry;

namespace
{
    // 保ちたい距離からこれだけずれると、前後が横と同じ強さ [m]
    constexpr float kRepositionRadialRange = 10.0f;
}



void BossActionCommon::UpdateRecovery(BossActionComponent &bossAction, float recoveryTime, const BossActionFrame &frame)
{
    frame.movement.desiredVelocity = Vector3::Zero;

    bossAction.recoveryTimer += frame.dt;
    if (bossAction.actionFinished || bossAction.recoveryTimer < recoveryTime) return;

    // BT はこれを見て次の行動へ進む
    // 長さを残すのは、インスペクタで技の長さを確かめるため
    bossAction.actionFinished = true;
    bossAction.lastActionTime = bossAction.actionTimer;
}



Vector3 BossActionCommon::KeepDistanceVector(const Vector3 &bossPos, const Vector3 &toPlayerDir, bool moveLeft,
                                             const BossActionComponent &bossAction, const BossActionFrame &frame)
{
    // --- 横の向き ---

    const Vector3 rightDir = RightOf(toPlayerDir);
    Vector3       sideDir  = rightDir;
    if (moveLeft) sideDir = -rightDir;

    // --- 距離を保つ前後 ---

    // 近すぎれば離れ、遠すぎれば詰める。+ が詰める、- が離れる
    // 保ちたい距離から外れているほど強くし、kRepositionRadialRange で横と同じ強さになる
    const float distanceToPlayer = FlatToward(bossPos, frame.playerPos).Length();

    // 距離を保つ前後の強さ。-1〜+1 で、0 がちょうど良い距離
    float       approachWeight   = 0.0f;

    // 保ちたい距離より近いときは、近いほど強く離れる
    if (distanceToPlayer < bossAction.repositionNearDistance)
    {
        approachWeight = -(bossAction.repositionNearDistance - distanceToPlayer) / kRepositionRadialRange;
    }
    // 保ちたい距離より遠いときは、遠いほど強く詰める
    if (distanceToPlayer > bossAction.repositionFarDistance)
    {
        approachWeight = (distanceToPlayer - bossAction.repositionFarDistance) / kRepositionRadialRange;
    }
    approachWeight = std::clamp(approachWeight, -1.0f, 1.0f);

    // --- 壁から離れる内向き ---

    // 壁に近いほど強く、ステージの中心へ寄る。長さが強さ（0〜1）
    // 壁際で真横へ回ると、壁に沿って動けなくなるため
    Vector3       inwardPush        = Vector3::Zero;
    const Vector3 bossToCenter      = FlatToward(bossPos, Vector3(frame.battle.arenaCenter));
    const float   distanceToWall    = frame.battle.arenaRadius - bossToCenter.Length();
    const float   wallAvoidDistance = bossAction.repositionWallDistance;

    // 壁の手前にいるときだけ、中心へ押す
    if (wallAvoidDistance > 0.0f && distanceToWall < wallAvoidDistance && bossToCenter.LengthSquared() > 0.0001f)
    {
        const float pushStrength = std::clamp((wallAvoidDistance - distanceToWall) / wallAvoidDistance, 0.0f, 1.0f);
        inwardPush               = bossToCenter / bossToCenter.Length() * pushStrength;
    }

    return sideDir + toPlayerDir * approachWeight + inwardPush;
}



Vector2 BossActionCommon::BodyLocalDirection(const Vector3 &moveDir, const TransformComponent &transform)
{
    // 体の正面と右（水平）
    Vector3 facingDir = Vector3::Transform(Vector3::UnitZ, transform.rotation);
    facingDir.y       = 0.0f;
    if (facingDir.LengthSquared() > 0.0001f) facingDir.Normalize();
    const Vector3 facingRightDir(facingDir.z, 0.0f, -facingDir.x);

    // 動く向きが、右と前へそれぞれどれだけ向いているか（-1〜1）
    return Vector2(moveDir.Dot(facingRightDir), moveDir.Dot(facingDir));
}
