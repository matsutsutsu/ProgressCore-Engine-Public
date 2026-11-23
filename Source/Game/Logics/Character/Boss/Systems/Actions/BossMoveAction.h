#pragma once
#include "BossActionCommon.h"

// ボスの待機と、相手へ歩いて近づく移動
class BossMoveAction
{
public:
    // 待機。足を止め、idleTurnSpeed でゆっくり相手へ向き直る（ひるみ中は向き直らない）
    void UpdateIdle(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 相手へ歩いて近づく。速さは Jolt の walkSpeed
    void UpdateMove(BossActionComponent &bossAction, const BossActionFrame &frame) const;
};
