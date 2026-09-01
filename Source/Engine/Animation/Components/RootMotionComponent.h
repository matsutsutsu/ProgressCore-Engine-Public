#pragma once

// 前方宣言
struct AnimSequence;

// ルートモーションの実行状態。
struct RootMotionComponent
{
    bool enabled = false; // 現ステートが applyRootMotion なら true

    // 外部システムがこのフレームの移動を制御中か（＝root motion抽出を止めたい）。
    // true の間、RootMotionSystem は root motion による移動を一切適用しない。
    // 「いつ true にするか」はゲーム側の移動アクション（例: ダッシュ）が毎フレーム設定する。
    // これにより RootMotionSystem はダッシュ等の特定機構を知らない汎用システムに保てる。
    bool externallyDriven = false;

    // 遷移検知用。前回評価したシーケンス
    const AnimSequence *lastEvaluatedSeq = nullptr;

    float prevTimer         = 0.0f; // 現在のモーションの前回評価時刻
    float prevPreviousTimer = 0.0f; // 過去のモーション（ブレンド元）の前回評価時刻
};
