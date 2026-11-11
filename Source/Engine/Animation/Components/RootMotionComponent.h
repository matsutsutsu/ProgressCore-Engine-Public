#pragma once
#include <cstdint>

struct AnimSequence;

// ルートモーションの実行状態。実行時の状態だけなので保存しない（Meta では空の枠だけ保存する）
struct RootMotionComponent
{
    bool enabled = false; // 今のステートが applyRootMotion なら true（RootMotionSystem が書く）

    // 外部の System がこのステップの移動を制御中か（ゲーム側の移動アクションが書く）
    //   true の間 RootMotionSystem は移動を適用しないので、ダッシュ等の個別の仕組みを知らずに済む
    bool externallyDriven = false;

    const AnimSequence *lastEvaluatedSeq = nullptr; // 前回評価したクリップ（切り替わり検知用）。所有しない
    uint32_t            lastPlayCount    = 0;       // 前回評価したときの Animator の再生回数

    float prevTimer         = 0.0f; // 現在のクリップの前回評価時刻（秒）
    float prevPreviousTimer = 0.0f; // ブレンド元のクリップの前回評価時刻（秒）
};
