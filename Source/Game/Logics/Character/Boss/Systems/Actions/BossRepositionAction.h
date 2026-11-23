#pragma once
#include <SimpleMath.h>

#include "BossActionCommon.h"

// ボスの間合いを取る。相手を向いたまま、入った瞬間に決めた向きへ進む
class BossRepositionAction
{
public:
    // 動く向きと段の長さを決め、加速を予約する
    // travelDistance は壁の手前までに縮める
    void Enter(float travelDistance, BossActionComponent &bossAction, const BossActionFrame &frame) const;

    // 動き出し → 一定の速さ → 止まり。段の移動が終わるたびに、次の段の移動を予約する
    void Update(BossActionComponent &bossAction, const BossActionFrame &frame) const;

private:
    // 動く向き（水平で長さ 1）。空きが広い側へ回り、前後への傾きは抑える
    // travelDistance は左右の空きを測る距離
    DirectX::SimpleMath::Vector3 RepositionDirection(float travelDistance, const BossActionComponent &bossAction,
                                                     const BossActionFrame &frame) const;
};
