#include "BossRepositionAction.h"
#include "BossActionGeometry.h"

#include <algorithm>
#include <cmath>

using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;

namespace
{
    // 間合いを取る移動の向きの決め方
    // 前後へはこの角度までしか傾けない。アニメが左右の移動しかないため
    constexpr float kRepositionMaxTiltDeg = 40.0f; // 真横からの傾きの上限 [度]
}



void BossRepositionAction::Enter(float travelDistance, BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 動く向きを、距離と壁から決める
    const Vector3 moveDir = RepositionDirection(travelDistance, bossAction, frame);

    // --- アニメの左右 ---

    // 相手から見て左寄りか右寄りかで、引くトリガー（SideTravelLeft / Right）を選ぶ
    // lockedDirection はまだ相手への向き（下で進む向きに書き換える）
    const Vector3 toPlayerDir  = bossAction.lockedDirection;
    bossAction.repositionLeft  = (moveDir.Dot(RightOf(toPlayerDir)) < 0.0f);
    bossAction.repositionPhase = BossRepositionPhase::Start;

    // --- 進む向きと距離 ---

    // 以後は進む向きを変えない。相手が動いても追わない
    bossAction.lockedDirection = moveDir;

    // 壁の手前で止まるよう、実際に動く距離に縮める
    const Vector3 bossPos      = GetCharacterWorldPosition(frame.joltHandle);
    const float   moveDistance = MovableDistance(bossPos, bossAction.lockedDirection, travelDistance, frame.battle);

    // --- 段の長さ ---

    // 加速と減速の間に進む距離を引いた残りを、最高速で進む
    // 距離が足りなければ最高速は落とさず、加速と減速を同じ割合で縮める（遅く動くより、早く止まる）
    const float maxSpeed     = (std::max)(1.0f, bossAction.repositionSpeed);
    float       accelTime    = (std::max)(0.0f, bossAction.repositionAccelTime);
    float       decelTime    = (std::max)(0.0f, bossAction.repositionDecelTime);
    const float rampDistance = maxSpeed * (accelTime + decelTime) * 0.5f;
    float       cruiseTime   = 0.0f;
    if (moveDistance >= rampDistance)
    {
        cruiseTime = (moveDistance - rampDistance) / maxSpeed;
    }
    else
    {
        const float shrinkRatio = moveDistance / rampDistance;
        accelTime *= shrinkRatio;
        decelTime *= shrinkRatio;
    }
    bossAction.repositionCruiseTime = cruiseTime;
    bossAction.repositionStopTime   = decelTime;

    // 0 から最高速へ加速する
    frame.movement.MoveInDirection(bossAction.lockedDirection, 0.0f, accelTime, maxSpeed);
}



void BossRepositionAction::Update(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // --- 段の切り替え ---

    // 予約した移動が終わったら、次の段の移動を予約する
    // 加速の移動は、入った瞬間に Enter が予約済み
    if (!frame.movement.IsMoving())
    {
        switch (bossAction.repositionPhase)
        {
        case BossRepositionPhase::Start:
            // 最高速のまま、残りの距離を進む
            // 残りがなければ減速へ直接進む（最高速の1フレームを挟むと、その分だけ進みすぎる）
            if (bossAction.repositionCruiseTime > 0.0f)
            {
                frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.repositionSpeed,
                                           bossAction.repositionCruiseTime, bossAction.repositionSpeed);
                bossAction.repositionPhase = BossRepositionPhase::Travel;
                break;
            }

            // 意図的にreturn書いてない、すぐにTravelに進みたいため
            [[fallthrough]];

        case BossRepositionPhase::Travel:
            // 最高速から 0 へ減速して止まる
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.repositionSpeed,
                                       bossAction.repositionStopTime, 0.0f);
            bossAction.repositionPhase = BossRepositionPhase::Stop;
            break;

        default:
            // 止まった。速度は予約が 0 にして止める
            bossAction.repositionPhase = BossRepositionPhase::End;
            break;
        }
    }

    // --- 終わり ---

    // 硬直は持たない。止まりのモーションがそのまま後隙になる
    if (bossAction.repositionPhase == BossRepositionPhase::End)
    {
        UpdateRecovery(bossAction, 0.0f, frame);
        return;
    }

    // --- 向き ---

    // 体は相手を向き続ける。進む向きは入った瞬間のまま変えない
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;

    // 体が回るので、体から見た進む向きは進んでいる間にも変わる。アニメは前後左右をこの向きで混ぜる
    bossAction.localMoveDirection = BodyLocalDirection(bossAction.lockedDirection, frame.transform);
}



Vector3 BossRepositionAction::RepositionDirection(float travelDistance, const BossActionComponent &bossAction,
                                                  const BossActionFrame &frame) const
{
    // lockedDirection は相手への向き（OnEnterState が入れた）
    const Vector3 toPlayerDir = bossAction.lockedDirection;
    const Vector3 rightDir    = RightOf(toPlayerDir);
    const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);

    // --- 横 ---

    // 左右それぞれ、壁の手前までに実際に動ける距離
    const float rightMoveDistance = MovableDistance(bossPos, rightDir, travelDistance, frame.battle);
    const float leftMoveDistance  = MovableDistance(bossPos, -rightDir, travelDistance, frame.battle);

    // 動ける距離が長い側へ回る
    // どちらも同じなら前回と同じ側。相手の周りを回り続ける
    bool moveLeft = bossAction.repositionLeft;
    if (leftMoveDistance > rightMoveDistance + 0.1f) moveLeft = true;
    if (rightMoveDistance > leftMoveDistance + 0.1f) moveLeft = false;

    // --- 合成 ---

    // 横 ＋ 距離を保つ前後 ＋ 壁から離れる内向き を足し、横の成分と前後の成分に分け直す
    // 前後の成分は + が相手へ近づく向き
    const Vector3 combined    = KeepDistanceVector(bossPos, toPlayerDir, moveLeft, bossAction, frame);
    float         sideAmount  = combined.Dot(rightDir);
    float         frontAmount = combined.Dot(toPlayerDir);

    // 横がほぼ消えたら、選んだ側へ戻す
    // 真前・真後ろへの移動は、左右へ動くアニメで見せられない
    if (std::fabs(sideAmount) < 0.01f)
    {
        sideAmount = 0.01f;
        if (moveLeft) sideAmount = -0.01f;
    }

    // 前後は、真横から kRepositionMaxTiltDeg 度の傾きまでに抑える
    const float maxFrontAmount = std::fabs(sideAmount) * std::tan(DirectX::XMConvertToRadians(kRepositionMaxTiltDeg));
    frontAmount                = std::clamp(frontAmount, -maxFrontAmount, maxFrontAmount);

    Vector3 moveDir = rightDir * sideAmount + toPlayerDir * frontAmount;
    moveDir.Normalize();
    return moveDir;
}
