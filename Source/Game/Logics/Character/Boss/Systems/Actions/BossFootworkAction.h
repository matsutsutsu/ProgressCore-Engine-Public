#pragma once
#include <random>

#include <SimpleMath.h>

#include "BossActionCommon.h"

// ボスの様子見。相手を向いたまま、間合いを保つ向きへ動き回る
class BossFootworkAction
{
public:
    // 1回の長さを下限〜上限から引き、相手の周りを回る側（空きが広い側。同じならランダム）を決める
    void Enter(BossActionComponent &bossAction, const BossActionFrame &frame);

    // 間合いを保つ向きを毎フレーム決め直して動く
    // 決めた長さが来たら actionFinished を立てるが、止まらずに動き続ける
    void Update(BossActionComponent &bossAction, const BossActionFrame &frame) const;

private:
    // アニメの向き（前後左右）を、体から見た動く向きで選ぶ
    // 今の向きより十分に近い向きが出たときだけ切り替える
    BossFootworkDir FootworkAnimDir(const DirectX::SimpleMath::Vector2 &localMove, BossFootworkDir currentDir) const;

    // 長さと回る側を引く乱数。BossActionSystem は1体ずつ順に呼ぶので、全個体で1つを使ってよい
    std::mt19937 m_random{std::random_device{}()};
};
