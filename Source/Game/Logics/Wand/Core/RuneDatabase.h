#pragma once
// ===========================================================================
// RuneDatabase.h
// --------------
// 役割:
//   ルーン(概念)ごとの「フェーズ別デフォルト挙動パラメータ」と「スタックカーブ」の
//   唯一の置き場(SSOT)。
//
// 責務:
//   - (EffectType, WandPhase) → EffectOp デフォルト値（GetDefaultOp）。
//   - (EffectType, WandPhase) → スタックカーブ（GetStackCurve）。カーブを持つ概念のみ。
//
// 設計方針:
//   - 調整値を各ハンドラ/コンパイラに散らさず、ここに一元化する。
//   - スタックカーブは「カーブを持つ概念だけ」定義する段階導入。HasStackCurve が
//     false の概念は従来通り params の累乗で扱う（既存挙動を壊さない）。
// ===========================================================================

#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Core/RuneStackCurve.h"

/**
 * @brief ルーンのフェーズ別デフォルト挙動・スタックカーブを引く静的テーブル。
 */
struct RuneDatabase
{
    /**
     * @brief 指定概念・指定フェーズの既定 EffectOp を返す。
     * @return params を既定値で埋めた EffectOp（stackCount は呼び出し側が上書き）。
     */
    static EffectOp GetDefaultOp(EffectType type, WandPhase phase);

    /**
     * @brief 指定概念・指定フェーズがスタックカーブを持つか。
     * @return true ならカーブで効果量を調整する。false なら従来の params 累乗。
     */
    static bool HasStackCurve(EffectType type, WandPhase phase);

    /**
     * @brief 指定概念・指定フェーズのスタックカーブを返す。
     * @note  HasStackCurve が true の組み合わせでのみ意味を持つ。
     */
    static RuneStackCurve GetStackCurve(EffectType type, WandPhase phase);
};