#pragma once
#include <SimpleMath.h>

#include "BossActionCommon.h"

// ボスの突進攻撃。下がる → 空中で狙う → 構える → 突っ込む → 走り続ける → 止まる → 硬直
class BossDashAction
{
public:
    // まっすぐ後ろの空中へ下がる移動を予約する。突っ込む方向は構えに入る瞬間に決める
    void Enter(BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 段の移動が終わるたびに、次の段の移動を予約する
    void Update(BossActionComponent &bossAction, const BossActionFrame &frame) const;

private:
    // 構え（Windup）に入る瞬間に1回だけ呼ぶ
    // 相手への方向を固定し、その方向へ dashDistance 進む終点と、突っ込む長さを決める
    void LockAim(BossActionComponent                &bossAction,
                 const DirectX::SimpleMath::Vector3 &playerPos,
                 const BattleContext                &battle) const;
};
