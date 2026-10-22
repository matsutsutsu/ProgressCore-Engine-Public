#pragma once
// ===========================================================================
// 概要: ルートモーションの実行状態を持つコンポーネント RootMotionComponent。
//
// 責務:
//   - 今のステートでルートモーションを使うか、外部が移動を制御中かを保持する
//   - 前回評価したクリップと時刻を保持する（差分を求めるため）
//
// 備考: 中身は実行時の状態だけなので保存しない（Meta では空の枠だけを保存する）。
// ===========================================================================

struct AnimSequence;

struct RootMotionComponent
{
    bool enabled = false; // 今のステートが applyRootMotion なら true（RootMotionSystem が書く）

    // 外部の System がこのステップの移動を制御中か。true の間、RootMotionSystem は移動を一切適用しない。
    //   ゲーム側の移動アクション（PlayerDashSystem など）が書く。
    //   こうすることで RootMotionSystem はダッシュ等の個別の仕組みを知らずに済む
    bool externallyDriven = false;

    const AnimSequence *lastEvaluatedSeq = nullptr; // 前回評価したクリップ（切り替わり検知用）。所有しない

    float prevTimer         = 0.0f; // 現在のクリップの前回評価時刻（秒）
    float prevPreviousTimer = 0.0f; // ブレンド元のクリップの前回評価時刻（秒）
};
