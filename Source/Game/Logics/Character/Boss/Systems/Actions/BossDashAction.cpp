#include "BossDashAction.h"
#include "BossActionGeometry.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;



void BossDashAction::Enter(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    bossAction.dashPhase = BossDashPhase::Retreat;

    // 下がって狙う地点（空中）を決める。まっすぐ後ろへ下がり、浮いたまま止まる
    // lockedDirection は相手への向きなので、その逆へ下がる
    const Vector3 bossPos = GetCharacterWorldPosition(frame.joltHandle);
    Vector3       aimPos  = StraightPointInArena(bossPos, -bossAction.lockedDirection, bossAction.dashRetreatDistance, frame.battle);
    aimPos.y              = bossPos.y + bossAction.dashRetreatHeight;

    bossAction.dashStartPos = bossPos;
    bossAction.dashAimPos   = aimPos;

    // 狙う地点まで、速く下がり始めて止まる移動を予約する
    const float retreatTime = (std::max)(0.05f, bossAction.dashRetreatTime);
    frame.movement.MoveTo(bossAction.dashAimPos, retreatTime, {.ease = MoveEase::EaseOut});
}



void BossDashAction::Update(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // --- 段の切り替え ---

    // 予約した移動が終わったら、次の段の移動を予約する
    // 下がる移動は、入った瞬間に Enter が予約済み
    if (!frame.movement.IsMoving())
    {
        switch (bossAction.dashPhase)
        {
        case BossDashPhase::Retreat:
            // 空中で止まり、相手を向いて狙う
            frame.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashAimTime));
            bossAction.dashPhase = BossDashPhase::Aim;
            break;

        case BossDashPhase::Aim:
            // 構えに入った瞬間に、突っ込む方向と終点を固定する
            // 以後は相手が動いても向きを変えない
            LockAim(bossAction, frame.playerPos, frame.battle);
            frame.movement.MoveTo(bossAction.dashAimPos, (std::max)(0.0f, bossAction.dashWindupTime));
            bossAction.dashPhase = BossDashPhase::Windup;
            break;

        case BossDashPhase::Windup:
            // 空中から地面の終点へ、等速でまっすぐ突っ込む
            frame.movement.MoveTo(bossAction.dashEndPos, bossAction.dashChargeTime);
            bossAction.dashPhase = BossDashPhase::Charge;
            break;

        case BossDashPhase::Charge:
            // 着地後も同じ向きへ走り続ける（壁に当たれば沿って滑る）
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
                                       bossAction.dashTravelTime, bossAction.dashTravelSpeed);
            bossAction.dashPhase = BossDashPhase::Travel;
            break;

        case BossDashPhase::Travel:
            // 走る速さから 0 へ、直線で落として止まる
            frame.movement.MoveInDirection(bossAction.lockedDirection, bossAction.dashTravelSpeed,
                                       (std::max)(0.0f, bossAction.dashStopTime), 0.0f);
            bossAction.dashPhase = BossDashPhase::Stop;
            break;

        default:
            // 止まった。速度は予約が 0 にして止める
            bossAction.dashPhase = BossDashPhase::Recovery;
            break;
        }
    }

    // --- 硬直 ---

    // 足を止め、向きは突進の向きのまま固定する
    // 振り向くと、避けたのに狙われ続けているように見える
    if (bossAction.dashPhase == BossDashPhase::Recovery)
    {
        // 怒り中は硬直を縮め、連発の間を詰める
        const float recovery = frame.raging ? bossAction.dashRecovery * bossAction.rageRecoveryScale : bossAction.dashRecovery;
        UpdateRecovery(bossAction, recovery, frame);
        return;
    }

    // --- 向き ---

    // 構えた後は、固定した方向を向く
    // ここで相手を追うと「構えた＝方向が決まった」と読めない
    Vector3 lookDir = bossAction.lockedDirection;

    // 下がる・狙う間は、相手を向き続ける
    // 構えに入った瞬間のこの向きが、突進の方向になる
    if (bossAction.dashPhase == BossDashPhase::Retreat || bossAction.dashPhase == BossDashPhase::Aim)
    {
        lookDir = FlatToward(GetCharacterWorldPosition(frame.joltHandle), frame.playerPos);
    }
    FaceToward(frame.transform, lookDir, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    bossAction.moveDirection = bossAction.lockedDirection;
}



void BossDashAction::LockAim(BossActionComponent &bossAction, const Vector3 &playerPos,
                             const BattleContext &battle) const
{
    // 狙うのは相手への「方向」だけ
    // 相手の位置で止めず、走り抜けた先で間合いを離す
    const Vector3 aimToPlayer = FlatToward(bossAction.dashAimPos, playerPos);

    // 真下に相手がいて向きが決まらないときは、下がる前の向きで突っ込む
    const float aimToPlayerDistance = aimToPlayer.Length();
    Vector3     chargeDir           = bossAction.lockedDirection;
    if (aimToPlayerDistance > 0.001f) chargeDir = aimToPlayer / aimToPlayerDistance;

    // 終点は構えた向きへまっすぐ dashDistance 先の地面（平らなステージ前提）
    // 空中から地面へ下りながら進む。壁際では向きを変えず、手前で止める
    Vector3 chargeEndPos = StraightPointInArena(bossAction.dashAimPos, chargeDir, bossAction.dashDistance, battle);
    chargeEndPos.y       = bossAction.dashStartPos.y;

    bossAction.dashEndPos      = chargeEndPos;
    bossAction.lockedDirection = chargeDir;

    // 突っ込む長さは、実際に走る距離（高さを含む）÷速さ
    const float chargeDistance = Vector3::Distance(bossAction.dashAimPos, bossAction.dashEndPos);
    bossAction.dashChargeTime  = chargeDistance / (std::max)(1.0f, bossAction.dashSpeed);
}
