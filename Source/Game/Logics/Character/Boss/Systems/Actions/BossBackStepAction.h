#pragma once
#include "BossActionCommon.h"

// ボスの後方ステップ。入った瞬間の相手の方向を向いたまま下がり、着地して硬直する
class BossBackStepAction
{
public:
    // 相手と逆の向きへ、まっすぐ下がる移動を予約する
    void Enter(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 下がり始め → 止まり → 着地硬直
    void Update(BossActionComponent &bossAction, const BossActionFrame &frame) const;
};
