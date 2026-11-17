#include "BossBackStepAction.h"
#include "BossActionGeometry.h"

#include "Engine/Animation/Core/AnimStateMachine.h"
#include "Engine/Core/Math/StringHash.h"

#include <algorithm>

using namespace DirectX::SimpleMath;
using namespace BossActionCommon;
using namespace BossActionGeometry;

namespace
{
    // アニメのステートが1回流れ切る長さ（クリップの長さ÷再生速度）を返す
    // サブマシンの中も探す。見つからなければ fallback
    float StatePlayTime(const AnimStateGraph &graph, uint32_t stateHash, float fallback)
    {
        for (const AnimState &state : graph.states)
        {
            if (state.stateHash == stateHash && state.sequence && state.playbackSpeed > 0.0f)
                return state.sequence->duration / state.playbackSpeed;
            if (state.isSubMachine && state.subGraph)
            {
                const float subPlayTime = StatePlayTime(*state.subGraph, stateHash, -1.0f);
                if (subPlayTime >= 0.0f) return subPlayTime;
            }
        }
        return fallback;
    }



    // 後方ステップの止まりのモーションを流すステート
    // （ValkyraAnimNode.json の Loco_BackStep_Stop）
    const uint32_t kBackStepStopState = CCL::Utils::HashString("Loco_BackStep_Stop");
}



void BossBackStepAction::Enter(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 進む向きを、相手と逆にする
    bossAction.lockedDirection = -bossAction.lockedDirection;
    bossAction.backStepPhase   = BossBackStepPhase::Retreat;

    // 止まりのモーションの長さを、アニメグラフから取る
    // 下がる長さ＝下がり始め＋止まりのモーション。取れなければ 0.4 秒
    bossAction.backStepStopTime = frame.animStateMachine ? StatePlayTime(frame.animStateMachine->internalGraph, kBackStepStopState, 0.4f) : 0.4f;

    const float retreatTime      = (std::max)(0.0f, bossAction.backStepRetreatTime);
    const float backStepMoveTime = (std::max)(0.05f, retreatTime + bossAction.backStepStopTime);

    // まっすぐ後ろの終点まで運ぶ予約をする
    // 速く下がり始めて終点で止まり、途中で少し浮いて地面に戻る
    const Vector3 bossPos        = GetCharacterWorldPosition(frame.joltHandle);
    const Vector3 backStepEndPos = StraightPointInArena(bossPos, bossAction.lockedDirection, bossAction.backStepDistance, frame.battle);
    frame.movement.MoveTo(backStepEndPos, backStepMoveTime, {.height = bossAction.backStepHeight, .ease = MoveEase::EaseOut});
}



void BossBackStepAction::Update(BossActionComponent &bossAction, const BossActionFrame &frame) const
{
    // 入った瞬間の相手の方向を向き続ける（後から追わない）
    // 背を向けると、間合いを取り直す技ではなく逃げたように見える
    FaceToward(frame.transform, -bossAction.lockedDirection, bossAction.turnSpeed, frame.dt, frame.joltHandle);

    // 下がり始め → 止まり の切り替えは、アニメのために時刻で決める
    const bool isRetreatTimeOver = bossAction.actionTimer >= bossAction.backStepRetreatTime;
    if (bossAction.backStepPhase == BossBackStepPhase::Retreat && isRetreatTimeOver)
        bossAction.backStepPhase = BossBackStepPhase::Stop;

    // 予約した移動が終わったら、着地硬直にする
    if (!frame.movement.IsMoving()) bossAction.backStepPhase = BossBackStepPhase::Recovery;

    if (bossAction.backStepPhase == BossBackStepPhase::Recovery)
    {
        UpdateRecovery(bossAction, bossAction.backStepRecovery, frame);
        return;
    }

    bossAction.moveDirection = bossAction.lockedDirection;
}
