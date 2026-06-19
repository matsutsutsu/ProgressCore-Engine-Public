#pragma once
// ===========================================================================
// RuneDatabase.h
// --------------
// 役割:
//   ルーン(概念)ごとの「フェーズ別デフォルトパラメータ」「スタックカーブ」
//   「カーブの畳み込み方」の唯一の置き場(SSOT)。
//
// 責務:
//   - GetDefaultOp  : (type, phase) → EffectOp の既定値
//   - HasStackCurve : (type, phase) → カーブを持つか
//   - GetStackCurve : (type, phase) → RuneStackCurve
//   - GetCurveFoldType : (type, phase) → 畳み込み方(倍率系/加算系)
//
// 設計方針:
//   コンパイラ(WandCompiler)はRuneDatabaseに問い合わせるだけ。
//   「どう畳むか」の知識はRuneDatabaseに閉じ、コンパイラは汎用ロジックのみ持つ。
// ===========================================================================
#include "Game/Logics/Wand/Core/WandTypes.h"
#include "Game/Logics/Wand/Core/RuneStackCurve.h"

/// @brief カーブの畳み込み方。RuneDatabase が概念ごとに返す。
enum class CurveFoldType
{
    Multiplicative, ///< 倍率系：At(0)*At(1)*... 掛け算で畳む（初速倍率など）
    Additive,       ///< 加算系：At(0)+At(1)*... 足し算で畳む（本数加算など）
};

struct RuneDatabase
{
    /// @brief (type, phase) の既定 EffectOp を返す。stackCount は呼び出し側が上書き。
    static EffectOp GetDefaultOp(EffectType type, WandPhase phase);

    /// @brief (type, phase) がスタックカーブを持つか。
    static bool HasStackCurve(EffectType type, WandPhase phase);

    /// @brief (type, phase) のスタックカーブを返す。
    static RuneStackCurve GetStackCurve(EffectType type, WandPhase phase);

    /// @brief (type, phase) のカーブ畳み込み方を返す。HasStackCurve が true の時だけ呼ぶ。
    static CurveFoldType GetCurveFoldType(EffectType type, WandPhase phase);
};