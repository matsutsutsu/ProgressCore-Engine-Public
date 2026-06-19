#pragma once
// ===========================================================================
// RuneStackCurve.h
// ----------------
// 役割:
//   アタッチメントの「装着数(スタック数)に応じた効果量」を表す固定値テーブル。
//   1個目・2個目・3個目…の値を直接数字で持ち、逓減カーブをデータで表現する。
//
// 責務:
//   - スタックindex → 効果値 の対応（At）と、テーブル範囲外の fallback の提供のみ。
//
// アーキテクチャ上の位置付け:
//   RuneDatabase が (EffectType, WandPhase) ごとに本カーブを返す。WandCompiler が
//   コンパイル時にカーブを評価して EffectOp.params へ「展開済みの最終値」を焼く。
//   → 実行時のハンドラはカーブを知らず、EffectOp を見るだけで済む(純粋・高速)。
//
// 設計方針:
//   - レベルデザイン目的なので「N個目の値」を数字で直接並べられる固定長テーブルにする。
//     数式(累乗・指数減衰)より、デザイナーが手で調整しやすい。
//   - MAX_STACK を超える分は fallback（1個あたりの定常値）を使う。装着数が想定を
//     超えても破綻せず、なだらかに頭打ちする。
//   - POD。RuneDatabase が値で返せる軽量構造。
// ===========================================================================
#include <cstdint>

/// @brief スタック数ごとの効果値テーブル。
struct RuneStackCurve
{
    static constexpr int MAX_STACK = 8; ///< テーブルが直接持つ最大スタック数

    /// @brief value[i] = (i+1)個目のスタックを足したときの効果値。
    float value[MAX_STACK] = {};

    /// @brief MAX_STACK を超えた分の1スタックあたりの値（頭打ち用）。
    float fallback = 0.0f;

    /// @brief stackIndex(0始まり) に対応する効果値を返す。範囲外は fallback。
    /// @param stackIndex 0=1個目, 1=2個目, ...
    float At(int stackIndex) const
    {
        if (stackIndex < 0) return value[0];
        if (stackIndex < MAX_STACK) return value[stackIndex];
        return fallback;
    }

    /// @brief 全フィールドを同一値 v で埋めたカーブを作る（カーブ不要な等倍効果用）。
    static RuneStackCurve Uniform(float v)
    {
        RuneStackCurve c;
        for (int i = 0; i < MAX_STACK; ++i) c.value[i] = v;
        c.fallback = v;
        return c;
    }
};