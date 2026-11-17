#include "BossFootworkAction.h"
#include "BossActionGeometry.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;

namespace
{
    // 様子見の回る側とアニメの向きの決め方
    // アニメの向きは、斜め45度の付近で毎フレーム入れ替わらないよう、差が付いたときだけ切り替える
    constexpr float kFootworkSideLookAhead     = 30.0f; // 回る側を決めるとき、左右の空きを見る距離 [m]
    constexpr float kFootworkAnimDirHysteresis = 0.2f;  // 今の向きより、内積がこれだけ大きい向きへ切り替える
}



void BossFootworkAction::Enter(BossActionComponent &bossAction, const BossActionFrame &frame)
{
    std::uniform_real_distribution<float> random01(0.0f, 1.0f);

    // --- 長さ ---

    // 長さを毎回変えて、次の技までのリズムを読まれにくくする
    const float minTime     = (std::max)(0.0f, bossAction.footworkMinTime);
    const float maxTime     = (std::max)(minTime, bossAction.footworkMaxTime);
    bossAction.footworkTime = minTime + (maxTime - minTime) * random01(m_random);

    // 前の様子見の向きを持ち越さない（切り替えの判定が前回の向きから始まる）
    bossAction.footworkAnimDir = BossFootworkDir::None;

    // --- 回る側 ---

    // lockedDirection は相手への向き（OnEnterState が入れた）
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir    = RightOf(toPlayerDir);
    const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);

    // 左右それぞれ、壁の手前までに動ける距離（kFootworkSideLookAhead まで）
    const float rightMoveDistance = MovableDistance(bossPos, rightDir, kFootworkSideLookAhead, frame.battle);
    const float leftMoveDistance  = MovableDistance(bossPos, -rightDir, kFootworkSideLookAhead, frame.battle);

    // 空きが広い側へ回る。どちらも空いていればランダム
    // 毎回同じ側へ回ると、相手に読まれる
    bossAction.footworkLeft = (random01(m_random) < 0.5f);
    if (leftMoveDistance > rightMoveDistance + 0.1f) bossAction.footworkLeft = true;
    if (rightMoveDistance > leftMoveDistance + 0.1f) bossAction.footworkLeft = false;
}



void BossFootworkAction::Update(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    const Vector3 bossPos     = GetCharacterWorldPosition(frame.joltHandle);
    Vector3       toPlayerDir = FlatToward(bossPos, frame.playerPos);

    // 相手と重なっていて向きが決まらないときは止まる
    if (toPlayerDir.LengthSquared() <= 0.0001f)
    {
        frame.movement.desiredVelocity = Vector3::Zero;
        return;
    }
    toPlayerDir.Normalize();

    // --- 動く向き ---

    // 間合いを保つ向きを、今の位置と相手の位置から毎フレーム出し直す
    Vector3 keepDistance = KeepDistanceVector(bossPos, toPlayerDir, bossAction.footworkLeft, bossAction, frame);

    // 壁に押し返されて、回っている側へ進めなくなったら、反対側へ切り返す
    // 切り返した後は壁の押す向きと回る向きがそろうので、すぐには戻らない
    const Vector3 rightDir = RightOf(toPlayerDir);
    Vector3       sideDir  = rightDir;
    if (bossAction.footworkLeft) sideDir = -rightDir;
    if (keepDistance.Dot(sideDir) < 0.0f)
    {
        bossAction.footworkLeft = !bossAction.footworkLeft;
        keepDistance            = KeepDistanceVector(bossPos, toPlayerDir, bossAction.footworkLeft, bossAction, frame);
    }

    Vector3 moveDir = keepDistance;
    if (moveDir.LengthSquared() > 0.0001f) moveDir.Normalize();

    // 向きと速さを渡す。速さを急に変えても、物理が加速・減速を付ける
    frame.movement.desiredVelocity = moveDir;
    frame.movement.overrideSpeed   = bossAction.footworkSpeed;
    bossAction.moveDirection       = moveDir;

    // --- 体の向き ---

    // 体は相手を向き続ける。動く向きとのずれは、アニメの前後左右で見せる
    //   体から見た向きは、ブレンドスペース（MoveX / MoveY）の入力にもなる
    FaceToward(frame.transform, toPlayerDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);
    bossAction.localMoveDirection = BodyLocalDirection(moveDir, frame.transform);
    bossAction.footworkAnimDir    = FootworkAnimDir(bossAction.localMoveDirection, bossAction.footworkAnimDir);

    // --- 終わり ---

    // 決めた長さが来たら、BT へ終わりを知らせる。止まらずに動き続ける
    // 次も様子見なら切れ目なくつながり、技なら待機に入って物理の減速で止まる
    if (!bossAction.actionFinished && bossAction.actionTimer >= bossAction.footworkTime)
    {
        bossAction.actionFinished = true;
        bossAction.lastActionTime = bossAction.actionTimer;
    }
}



BossFootworkDir BossFootworkAction::FootworkAnimDir(const Vector2 &localMove, BossFootworkDir currentDir) const
{
    const float rightAmount = localMove.x;
    const float frontAmount = localMove.y;

    // 4方向それぞれの近さ。様子見していない（None）は一番遠い扱い
    auto closenessOf = [&](BossFootworkDir dir) {
        switch (dir)
        {
        case BossFootworkDir::Forward: return frontAmount;
        case BossFootworkDir::Back:    return -frontAmount;
        case BossFootworkDir::Left:    return -rightAmount;
        case BossFootworkDir::Right:   return rightAmount;
        default:                       return -1.0f;
        }
    };

    BossFootworkDir closestDir = BossFootworkDir::Forward;
    for (BossFootworkDir dir : {BossFootworkDir::Back, BossFootworkDir::Left, BossFootworkDir::Right})
    {
        if (closenessOf(dir) > closenessOf(closestDir)) closestDir = dir;
    }

    if (closenessOf(closestDir) > closenessOf(currentDir) + kFootworkAnimDirHysteresis) return closestDir;
    return currentDir;
}
