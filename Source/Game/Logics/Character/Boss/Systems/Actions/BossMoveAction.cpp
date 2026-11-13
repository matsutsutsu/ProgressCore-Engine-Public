#include "BossMoveAction.h"
#include "BossActionGeometry.h"

using namespace DirectX::SimpleMath;
using namespace BossActionGeometry;



void BossMoveAction::UpdateIdle(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 足を止める
    // 止まり方の慣性は、物理側（Jolt の deceleration）が付ける
    frame.movement.desiredVelocity = Vector3::Zero;

    // ひるみ中は向き直らない（ひるみの前後左右と、体の向きがずれる）
    if (bossAction.isFlinching) return;

    // 相手の方へ、移動中より遅く向き直る
    // すぐ正対すると、背後を取る意味がなくなる
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.idleTurnSpeed, frame.dt,
               frame.joltHandle);
}



void BossMoveAction::UpdateMove(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    Vector3 toPlayerDir = FlatToward(frame.transform.position, frame.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    if (toPlayerDir.LengthSquared() <= 0.001f)
    {
        frame.movement.desiredVelocity = Vector3::Zero;
        return;
    }

    toPlayerDir.Normalize();

    // 相手の方へ歩く
    // overrideSpeed を書かないので、物理側が Jolt の walkSpeed で動かす
    frame.movement.desiredVelocity = toPlayerDir;

    // toPlayerDir の方向へ、turnSpeed の速さで向き直る
    FaceToward(frame.transform, toPlayerDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);
}
