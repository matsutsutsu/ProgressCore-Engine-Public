#pragma once
// ===========================================================================
// RuneDatabase.h
// --------------
// @file
// @brief ルーン(概念)ごとの「フェーズ別デフォルト挙動パラメータ」の唯一の置き場(SSOT)。
//
// 役割:
//   (EffectType, WandPhase) → EffectOp のデフォルト値 を返す単一の真実源。
//   コンパイラ(WandCompiler)はここから既定 params を取得し、装着数を stackCount に詰める。
//
// @note ここを一元化することで、調整値が各ハンドラやコンパイラに散らばるのを防ぐ。
//       将来 JSON 等の外部データからロードする差し替えも、ここを窓口にすれば局所化できる。
// ===========================================================================

#include "WandTypes.h"

/**
 * @brief ルーンのフェーズ別デフォルト挙動を引くための静的テーブル。
 */
struct RuneDatabase
{
    /**
     * @brief 指定概念・指定フェーズの既定 EffectOp を返す。
     * @param type  効果概念
     * @param phase 装着フェーズ
     * @return params を既定値で埋めた EffectOp（stackCount は呼び出し側が上書きする）。
     *         未実装の組み合わせは type のみ設定し params=0 を返す。
     */
    static EffectOp GetDefaultOp(EffectType type, WandPhase phase);
};