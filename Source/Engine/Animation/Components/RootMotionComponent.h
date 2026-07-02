#pragma once

// 前方宣言
struct AnimSequence;

/// @brief ルートモーションの実行状態。
struct RootMotionComponent
{
    bool enabled = false; ///< 現ステートが applyRootMotion なら true

    /// @brief 遷移検知用。前回評価したシーケンス
    const AnimSequence *lastEvaluatedSeq = nullptr;

    float prevTimer         = 0.0f; ///< 現在のモーションの前回評価時刻
    float prevPreviousTimer = 0.0f; ///< 過去のモーション（ブレンド元）の前回評価時刻
};