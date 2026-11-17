#pragma once
// ===========================================================================
// 概要: UtilitySelector の子の点数を出す
//
// 責務:
//   - 採点ルール（BTScoreRule）と相手までの距離から、子の点数を計算する
//   - 同じ技を続けて出さないための、繰り返しの減点の倍率を計算する
//
// 備考: 入力だけで答えが決まる関数だけを置く（状態を持たない）
//       評価のワーカースレッドから並列に呼ばれるので、共有の状態を書いてはいけない
//       状況による倍率の条件の評価と、全部を掛け合わせるのは BehaviorTreeSystem::ScoreUtilityChild
//       （条件を評価するには System が作る BTContext が要るため）
// ===========================================================================
#include "Game/Logics/AI/BehaviorTree/Data/BehaviorTreeData.h"

namespace CCL::AI::BT
{
    // UtilitySelector が抽選する範囲（点数の高い順に何個までか）。エディタのプレビューも同じ値を使う
    inline constexpr int kUtilityTopCount = 3;

    // 技を選んでから繰り返しの減点が消えるまでの秒数。選んだ子の repeatTimers にこの値を入れる
    inline constexpr float kRepeatPenaltySeconds = 10.0f;

    // 子の点数のうち「優先度 × 距離の台形」の部分。ルールがなければ weight。0 以下は候補に入れない
    // outDistance を渡すと距離の倍率を書く（ルールがなければ -1）。デバッグ表示の内訳に使う
    float ScoreChild(const BTAsset &asset, BTNodeID childId, float distanceToTarget, float *outDistance = nullptr);

    // 距離の台形の値（0〜1）。得意な範囲で 1、そこから falloff 離れると 0、その間は直線でつなぐ
    float DistanceFactor(const BTScoreRule &rule, float distance);

    // 繰り返しの減点の倍率（0.3〜1）。選んだ直後が一番低く、remainingSeconds が 0 に近づくほど 1 へ戻る
    float RepeatFactor(float remainingSeconds);
}
