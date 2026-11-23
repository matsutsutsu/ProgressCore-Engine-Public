#include "BossLeapAction.h"
#include "BossActionGeometry.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;

namespace
{
    // 跳躍の頂点の高さを、相手との距離で上下させる範囲
    // 基準は BossLeapDef::height。近い相手へは低く、遠い相手へは高く跳ぶ
    constexpr float kLeapHeightNearDistance = 25.0f;  // この距離以下で一番低くなる [m]
    constexpr float kLeapHeightFarDistance  = 80.0f;  // この距離以上で一番高くなる [m]
    constexpr float kLeapHeightOffsetMin    = 0.0f; // 一番近いときに基準へ足す高さ [m]
    constexpr float kLeapHeightOffsetMax    = 10.0f;  // 一番遠いときに基準へ足す高さ [m]
}



// ---------------------------------------------------------------------------
// 入る・毎フレーム
// ---------------------------------------------------------------------------

void BossLeapAction::Enter(const BossLeapDef *leapDef, BossActionComponent &bossAction) const
{
    bossAction.leapPhase = BossLeapPhase::Windup;
    if (leapDef) bossAction.leapDef = *leapDef;
}



void BossLeapAction::Update(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    if (bossAction.leapPhase == BossLeapPhase::Windup)
    {
        UpdateWindup(bossAction, frame);

        // まだ溜めている。踏み切ったフレームは、そのまま空中の処理へ進む
        if (bossAction.leapPhase == BossLeapPhase::Windup) return;
    }

    if (!frame.movement.IsMoving()) AdvancePhase(bossAction, frame);

    if (bossAction.leapPhase == BossLeapPhase::Recovery)
    {
        UpdateLanding(bossAction, frame);
    }
    else
    {
        UpdateAir(bossAction, frame);
    }
}



void BossLeapAction::UpdateWindup(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 溜め中は、相手の方を向き続ける
    frame.movement.desiredVelocity = Vector3::Zero;
    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.turnSpeed, frame.dt, frame.joltHandle);

    if (bossAction.actionTimer < bossAction.leapDef.windup) return;

    TakeOff(bossAction, frame);
}



void BossLeapAction::UpdateAir(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    Vector3 lookDir = bossAction.lockedDirection;
    if (bossAction.leapDef.kind == BossLeapKind::Hover)
    {
        lookDir = FlatToward(frame.transform.position, frame.playerPos);
    }
    FaceToward(frame.transform, lookDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



void BossLeapAction::UpdateLanding(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    const bool isAttack = (bossAction.leapDef.kind == BossLeapKind::Attack);

    // 怒り中の跳躍攻撃は着地硬直を縮め、連発の間を詰める（逃げる跳躍・滞空は縮めない）
    const bool  shorten  = frame.raging && isAttack;
    const float recovery = shorten ? bossAction.leapDef.landRecovery * bossAction.rageRecoveryScale : bossAction.leapDef.landRecovery;
    UpdateRecovery(bossAction, recovery, frame);

    // 跳躍攻撃の硬直中は向きを固定する
    // 相手を追うと、横へ避けても判定が付いてきて後隙を攻撃できない
    if (isAttack) return;

    FaceToward(frame.transform, FlatToward(frame.transform.position, frame.playerPos), bossAction.idleTurnSpeed, frame.dt, frame.joltHandle);
}



// ---------------------------------------------------------------------------
// 踏み切りと段の切り替え
// ---------------------------------------------------------------------------

void BossLeapAction::TakeOff(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 処理フロー:
    // 1. 踏み切り地点と着地点を決める
    // 2. 飛ぶ向き（体を向ける向き）を固定する
    // 3. 相手との距離から、頂点の高さを決める
    // 4. 最初の移動を予約する（逃げるは着地まで、跳躍攻撃・滞空は頂点の段まで）

    // --- 踏み切り地点と着地点 ---

    // 踏み切った地点を覚える（跳躍中のドローンの陣形の高さと、着地点の高さに使う）
    const Vector3 takeoffPos  = GetCharacterWorldPosition(frame.joltHandle);
    bossAction.leapTakeoffPos = takeoffPos;
    bossAction.leapLandingPos = LandingPoint(bossAction, takeoffPos, frame);

    // --- 飛ぶ向き ---

    // 踏み切り→着地点で固定する
    Vector3 flightDir = FlatToward(takeoffPos, bossAction.leapLandingPos);
    if (flightDir.LengthSquared() > 0.0001f)
    {
        flightDir.Normalize();
        bossAction.lockedDirection = flightDir;
    }

    // --- 頂点の高さ ---

    // 踏み切った瞬間の相手との距離で、近いほど低く、遠いほど高くする
    const float distanceToPlayer = FlatToward(takeoffPos, frame.playerPos).Length();
    const float farRatio         = FarRatio(distanceToPlayer);
    const float apexHeight       = ApexHeight(bossAction.leapDef.height, farRatio);

    // --- 最初の移動 ---

    bossAction.leapPhase = BossLeapPhase::Flight;

    switch (bossAction.leapDef.kind)
    {
    case BossLeapKind::Escape: {
        // 着地点まで放物線で1回で飛ぶ
        const float flightTime = (std::max)(0.05f, bossAction.leapDef.flight);
        frame.movement.MoveTo(bossAction.leapLandingPos, flightTime, {.height = apexHeight});
        break;
    }

    case BossLeapKind::Attack: {
        // 放物線の上りで、頂点の手前（ゆっくりになり始める所）まで上がる
        const AttackLeapPlan plan = PlanAttackLeap(bossAction, apexHeight);
        bossAction.leapHangEndPos = plan.hangEndPos;
        bossAction.leapHangSpeed  = plan.hangSpeed;
        frame.movement.MoveTo(plan.hangStartPos, plan.riseTime, {.verticalStartSpeed = plan.riseStartSpeed});
        break;
    }

    case BossLeapKind::Hover: {
        // 真上の頂点まで速く上がって止まる
        Vector3 apexPos = Vector3::Lerp(takeoffPos, bossAction.leapLandingPos, 0.5f);
        apexPos.y       = takeoffPos.y + apexHeight;
        frame.movement.MoveTo(apexPos, (std::max)(0.05f, bossAction.leapDef.riseTime), {.ease = MoveEase::EaseOut});
        break;
    }
    }
}



BossLeapAction::AttackLeapPlan BossLeapAction::PlanAttackLeap(const BossActionComponent &bossAction, float apexHeight) const
{
    AttackLeapPlan plan;

    // --- ふつうの重力 ---

    const float sideTime     = (std::max)(0.05f, bossAction.leapDef.descentTime);                 // 上り・降りの時間
    const float hangTime     = (std::max)(0.01f, bossAction.leapDef.hover);                       // ゆっくりの時間
    const float gravityRatio = std::clamp(bossAction.leapDef.hangGravityRatio, 0.01f, 1.0f);      // ゆっくりの間の重力の割合

    // 頂点がちょうど apexHeight になる重力
    const float heightPerGravity = sideTime * sideTime * 0.5f + gravityRatio * hangTime * sideTime * 0.5f +
                                   gravityRatio * hangTime * hangTime * 0.125f;
    const float gravity = apexHeight / heightPerGravity;

    // --- ゆっくりの出入り ---

    // ゆっくりの出入りの縦の速さと、その位置の高さ
    plan.hangSpeed             = gravity * gravityRatio * hangTime * 0.5f;
    plan.riseStartSpeed        = plan.hangSpeed + gravity * sideTime;
    plan.riseTime              = sideTime;
    const float hangEdgeHeight = plan.hangSpeed * sideTime + 0.5f * gravity * sideTime * sideTime;

    // --- 横の途中の位置 ---

    // 横は一定の速さなので、時間の割合で途中の位置を決める
    const float totalTime = sideTime * 2.0f + hangTime;
    plan.hangStartPos     = Vector3::Lerp(bossAction.leapTakeoffPos, bossAction.leapLandingPos, sideTime / totalTime);
    plan.hangEndPos       = Vector3::Lerp(bossAction.leapTakeoffPos, bossAction.leapLandingPos, (sideTime + hangTime) / totalTime);
    plan.hangStartPos.y   = bossAction.leapTakeoffPos.y + hangEdgeHeight;
    plan.hangEndPos.y     = plan.hangStartPos.y;
    return plan;
}



void BossLeapAction::AdvancePhase(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 処理フロー:
    // 1. 上り終えたら、頂点の段へ（跳躍攻撃はゆっくり進む、滞空はその場に留まる）
    // 2. 頂点の段を終えたら、着地点へ降りる
    // 3. それ以外（逃げる・降り終えた）は着地にする

    const BossLeapKind kind        = bossAction.leapDef.kind;
    const float        descentTime = (std::max)(0.05f, bossAction.leapDef.descentTime);

    // --- 上り終えた ---

    if (bossAction.leapPhase == BossLeapPhase::Flight && kind == BossLeapKind::Attack)
    {
        // 横は同じ速さのまま、弱い重力で少し浮いてから戻る
        frame.movement.MoveTo(bossAction.leapHangEndPos, (std::max)(0.01f, bossAction.leapDef.hover),
                              {.verticalStartSpeed = bossAction.leapHangSpeed});
        bossAction.leapPhase = BossLeapPhase::Hover;
        return;
    }
    if (bossAction.leapPhase == BossLeapPhase::Flight && kind == BossLeapKind::Hover)
    {
        // その場に留まる
        frame.movement.Hold(bossAction.leapDef.hover);
        bossAction.leapPhase = BossLeapPhase::Hover;
        return;
    }

    // --- 頂点の段を終えた ---

    if (bossAction.leapPhase == BossLeapPhase::Hover && kind == BossLeapKind::Attack)
    {
        // ふつうの重力に戻り、上りの鏡写しの弧で着地点へ落ちる
        frame.movement.MoveTo(bossAction.leapLandingPos, descentTime, {.verticalStartSpeed = -bossAction.leapHangSpeed});
        bossAction.leapPhase = BossLeapPhase::Descent;
        return;
    }
    if (bossAction.leapPhase == BossLeapPhase::Hover)
    {
        // 真下の着地点へ。ゆっくり降り始めて、着地点で一番速くなる
        frame.movement.MoveTo(bossAction.leapLandingPos, descentTime, {.ease = MoveEase::EaseIn});
        bossAction.leapPhase = BossLeapPhase::Descent;
        return;
    }

    // --- 着地 ---

    // 速度は予約が 0 にして止める
    bossAction.leapPhase = BossLeapPhase::Recovery;
}



// ---------------------------------------------------------------------------
// 高さと着地点
// ---------------------------------------------------------------------------

float BossLeapAction::FarRatio(float distanceToPlayer) const
{
    const float distanceRange = kLeapHeightFarDistance - kLeapHeightNearDistance;
    return std::clamp((distanceToPlayer - kLeapHeightNearDistance) / distanceRange, 0.0f, 1.0f);
}



float BossLeapAction::ApexHeight(float baseHeight, float farRatio) const
{
    const float heightOffset = kLeapHeightOffsetMin + (kLeapHeightOffsetMax - kLeapHeightOffsetMin) * farRatio;

    // 基準が低い跳躍でも、地面より下を通らないようにする
    return (std::max)(0.0f, baseHeight + heightOffset);
}



Vector3 BossLeapAction::LandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos, const BossActionFrame &frame) const
{
    Vector3 landingPos = takeoffPos;
    switch (bossAction.leapDef.kind)
    {
    case BossLeapKind::Escape:
        landingPos = EscapeLandingPoint(bossAction, takeoffPos, frame.playerPos, frame.battle);
        break;
    case BossLeapKind::Attack:
        landingPos = AttackLandingPoint(bossAction, takeoffPos, frame.playerPos);
        break;
    case BossLeapKind::Hover:
        landingPos = takeoffPos; // 上がって同じ所へ降りる
        break;
    }

    // ステージの内側へ収める
    // 壁の外だと空中で壁を押し続け、着地の時刻と攻撃判定がずれる
    landingPos = ClampInsideArena(landingPos, frame.battle);

    // 高さは踏み切りの地面にそろえる（平らなステージ前提）
    landingPos.y = bossAction.leapTakeoffPos.y;
    return landingPos;
}



Vector3 BossLeapAction::EscapeLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                           const Vector3 &playerPos, const BattleContext &battle) const
{
    Vector3 arenaCenterPos(battle.arenaCenter);
    arenaCenterPos.y = takeoffPos.y;

    // 相手→中央の向きへ抜ける
    // 相手から見ると、頭上を越えて距離を取られる形になる
    Vector3 awayDir = FlatToward(playerPos, arenaCenterPos);

    // 相手がちょうど中央にいるときは、自分→中央で代用する
    if (awayDir.LengthSquared() <= 0.0001f)
    {
        awayDir = FlatToward(takeoffPos, arenaCenterPos);
    }
    if (awayDir.LengthSquared() <= 0.0001f) return arenaCenterPos; // 全員が中央に重なっている
    awayDir.Normalize();

    // 割合は 0.9 で頭打ちにする
    // 1 に近いと反対側の端に降り、今度はそちらで追い込まれる
    const float landingRatio = (std::max)(0.0f, (std::min)(bossAction.escapeLandingRatio, 0.9f));
    return arenaCenterPos + awayDir * (battle.arenaRadius * landingRatio);
}



Vector3 BossLeapAction::AttackLandingPoint(const BossActionComponent &bossAction, const Vector3 &takeoffPos,
                                           const Vector3 &playerPos) const
{
    const Vector3 takeoffToPlayer = FlatToward(takeoffPos, playerPos);

    const float distanceToPlayer = takeoffToPlayer.Length();
    if (distanceToPlayer <= 0.001f) return takeoffPos; // 真上にいる。その場で跳ぶ

    // 手前で止めるぶんだけ引く。近すぎれば 0 ＝その場で跳んで叩く
    const float landingDistance = (std::max)(0.0f, distanceToPlayer - bossAction.attackStandOff);
    return takeoffPos + takeoffToPlayer * (landingDistance / distanceToPlayer);
}
